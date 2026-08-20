//! Safe wrapper over the WireLab C ABI.
//!
//! Two invariants are enforced by the type system rather than by discipline:
//!
//! * **Thread affinity.** [`Session`] holds a raw pointer and is neither `Send`
//!   nor `Sync`, matching the C header's "one session belongs to one thread".
//! * **Borrow lifetime.** Every view borrows from session-owned storage that
//!   the next mutating call invalidates. Getters take `&self` and hand back
//!   values tied to that borrow; commands take `&mut self`. The borrow checker
//!   therefore rejects holding a row across a mutation -- the boundary's
//!   sharpest edge, made impossible to walk into.

// The boundary mirrors the whole C ABI, not the subset today's pages happen to
// read: every dirty bit, every row field, and the layout entry points only the
// test calls. A binding trimmed to its current callers is how ABI drift hides,
// so the unused half stays and is silenced here.
#![allow(dead_code)]

pub mod raw;

use std::ffi::CString;
use std::marker::PhantomData;
use std::ptr::NonNull;

pub use raw::{MetricSample, WIRELAB_FFI_ABI_VERSION};

/// One bit per category of change, drained by [`Session::take_dirty`].
///
/// Hand-written rather than pulled from `bitflags`: eight constants and three
/// operations do not justify a dependency.
#[derive(Clone, Copy, PartialEq, Eq, Default, Debug)]
pub struct Dirty(pub u32);

impl Dirty {
    pub const NONE: Self = Self(0);
    pub const TOPOLOGY: Self = Self(raw::WIRELAB_DIRTY_TOPOLOGY);
    pub const SELECTION: Self = Self(raw::WIRELAB_DIRTY_SELECTION);
    pub const STATUS: Self = Self(raw::WIRELAB_DIRTY_STATUS);
    pub const TRAFFIC_STATE: Self = Self(raw::WIRELAB_DIRTY_TRAFFIC_STATE);
    pub const TELEMETRY: Self = Self(raw::WIRELAB_DIRTY_TELEMETRY);
    pub const FAULTS: Self = Self(raw::WIRELAB_DIRTY_FAULTS);
    pub const POLICIES: Self = Self(raw::WIRELAB_DIRTY_POLICIES);
    pub const REPORT: Self = Self(raw::WIRELAB_DIRTY_REPORT);

    #[inline]
    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    #[inline]
    #[must_use]
    pub const fn intersects(self, other: Self) -> bool {
        (self.0 & other.0) != 0
    }

    #[inline]
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }
}

impl core::ops::BitOr for Dirty {
    type Output = Self;

    #[inline]
    fn bitor(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

impl core::ops::BitOrAssign for Dirty {
    #[inline]
    fn bitor_assign(&mut self, other: Self) {
        self.0 |= other.0;
    }
}

macro_rules! from_u32_enum {
    ($(#[$outer:meta])* pub enum $name:ident { $($variant:ident = $value:expr),* $(,)? } default $fallback:ident) => {
        $(#[$outer])*
        #[derive(Clone, Copy, PartialEq, Eq, Debug)]
        pub enum $name {
            $($variant = $value),*
        }

        impl From<u32> for $name {
            fn from(value: u32) -> Self {
                match value {
                    $($value => Self::$variant,)*
                    // The C side static_asserts its enums against the core's, so
                    // an unknown value can only mean a mismatched binary; the
                    // layout check catches that far more loudly than a panic in
                    // the middle of a frame would.
                    _ => Self::$fallback,
                }
            }
        }
    };
}

from_u32_enum! {
    pub enum NodeType {
        Host = 0,
        Switch = 1,
    }
    default Host
}

from_u32_enum! {
    pub enum SelectionKind {
        None = 0,
        Node = 1,
        Link = 2,
    }
    default None
}

from_u32_enum! {
    pub enum Classification {
        Malformed = 0,
        Broadcast = 1,
        UnknownUnicast = 2,
        KnownUnicast = 3,
    }
    default Malformed
}

from_u32_enum! {
    pub enum Validity {
        Valid = 0,
        MalformedEthernet = 1,
        MalformedIpv4 = 2,
        MalformedTransport = 3,
    }
    default MalformedEthernet
}

from_u32_enum! {
    pub enum AnomalyType {
        BroadcastStorm = 0,
        MacFlap = 1,
        UnknownUnicastFlood = 2,
        UdpFlood = 3,
        PortScan = 4,
        HotTalker = 5,
        MalformedFrame = 6,
    }
    default BroadcastStorm
}

from_u32_enum! {
    pub enum PolicyAction {
        Allow = 0,
        Drop = 1,
        Mirror = 2,
        RateLimit = 3,
        Quarantine = 4,
        AlertOnly = 5,
    }
    default AlertOnly
}

from_u32_enum! {
    pub enum EnforcementKind {
        None = 0,
        RateLimit = 1,
        Blackhole = 2,
        Isolate = 3,
    }
    default None
}

from_u32_enum! {
    pub enum EnforcementOutcome {
        Applied = 0,
        Extended = 1,
        Released = 2,
        Skipped = 3,
        UnknownPort = 4,
        Rejected = 5,
    }
    default Skipped
}

/// Borrowed for as long as the `&Session` that produced it.
///
/// # Safety
/// The C side guarantees UTF-8 and a live pointer for the duration of the
/// borrow. A null pointer means "no value" and reads as the empty string; it is
/// never passed to `slice::from_raw_parts`, which would be undefined behaviour
/// even at length zero.
fn borrowed<'a>(value: raw::WirelabStr) -> &'a str {
    if value.ptr.is_null() || value.len == 0 {
        return "";
    }
    // SAFETY: see the doc comment; the lifetime is tied to the caller's &Session.
    unsafe {
        let bytes = std::slice::from_raw_parts(value.ptr.cast::<u8>(), value.len);
        std::str::from_utf8_unchecked(bytes)
    }
}

fn owned(value: raw::WirelabStr) -> String {
    borrowed(value).to_owned()
}

#[derive(Clone, Copy, Debug)]
pub struct Node<'a> {
    pub id: &'a str,
    pub node_type: NodeType,
    /// Normalised layout coordinates in `[0, 1]`.
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Copy, Debug)]
pub struct Link<'a> {
    pub from: &'a str,
    pub to: &'a str,
    pub latency_ms: i64,
}

#[derive(Clone, Copy, Debug)]
pub struct MacEntry<'a> {
    pub mac: &'a str,
    pub port: &'a str,
}

#[derive(Clone, Copy, Debug)]
pub struct PortState<'a> {
    pub id: &'a str,
    pub enforced: bool,
    pub received: u64,
    pub forwarded: u64,
    pub dropped: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct Packet<'a> {
    pub source_mac: &'a str,
    pub destination_mac: &'a str,
    pub source_ip: &'a str,
    pub destination_ip: &'a str,
    pub ingress: &'a str,
    pub protocol: u8,
    pub destination_port: u16,
    pub bytes: u16,
    pub classification: Classification,
    pub validity: Validity,
}

#[derive(Clone, Copy, Debug)]
pub struct Anomaly<'a> {
    pub anomaly_type: AnomalyType,
    pub source_mac: &'a str,
    pub source_ip: &'a str,
    pub ingress_port: u32,
    pub observed: u64,
    pub threshold: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct Fault<'a> {
    pub first: &'a str,
    /// Empty for a port fault.
    pub second: &'a str,
    pub latency_ms: i64,
    pub loss_percent: f64,
    pub blackhole: bool,
    pub is_link: bool,
}

#[derive(Clone, Copy, Debug)]
pub struct Policy<'a> {
    pub name: &'a str,
    pub anomaly_type: AnomalyType,
    pub action: PolicyAction,
    pub enabled: bool,
    pub rate_limit_packets_per_second: u64,
    pub hits: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct PolicyActionRow<'a> {
    pub sequence: u64,
    pub rule: &'a str,
    pub anomaly_type: AnomalyType,
    pub action: PolicyAction,
    pub port: &'a str,
    pub outcome: EnforcementOutcome,
    pub detail: &'a str,
}

#[derive(Clone, Copy, Debug)]
pub struct EnforcedPort<'a> {
    pub port: &'a str,
    pub rule: &'a str,
    pub kind: EnforcementKind,
    pub summary: &'a str,
}

#[derive(Clone, Copy, Debug)]
pub struct ReportRow<'a> {
    pub backend_label: &'a str,
    pub backend_id: &'a str,
    pub scenario: &'a str,
    pub packets: u64,
    pub elapsed_ns: u64,
    pub packets_per_second: f64,
    pub goodput_bits_per_second: f64,
    pub loss_percent: f64,
    pub latency_p50_ns: u64,
    pub latency_p95_ns: u64,
    pub latency_p99_ns: u64,
    pub host_to_device_ns: u64,
    pub kernel_ns: u64,
    pub device_to_host_ns: u64,
    pub transfer_inclusive_ns: u64,
    pub queue_wait_ns: u64,
    pub speedup: f64,
}

/// Owned, because the Reports pane keeps it across ticks.
#[derive(Clone, Debug, Default)]
pub struct Provenance {
    pub scenario: String,
    pub seed: u64,
    pub packets: u64,
    pub batch_size: u64,
    pub frame_size: u64,
    pub host_count: u64,
    pub generator: String,
    pub version: String,
    pub build_type: String,
    pub generated_at: String,
    pub backends_compiled_in: Vec<String>,
    pub backends_present: Vec<String>,
}

/// Every `_count`/`_at` pair collapses into one of these.
pub struct Rows<'a, T> {
    session: &'a Session,
    index: usize,
    len: usize,
    fetch: fn(&'a Session, usize) -> T,
}

impl<'a, T> Iterator for Rows<'a, T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        if self.index >= self.len {
            return None;
        }
        let item = (self.fetch)(self.session, self.index);
        self.index += 1;
        Some(item)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.len - self.index;
        (remaining, Some(remaining))
    }
}

impl<T> ExactSizeIterator for Rows<'_, T> {}

macro_rules! rows {
    ($method:ident, $item:ty, $count:ident, $at:ident, $convert:expr) => {
        pub fn $method(&self) -> Rows<'_, $item> {
            Rows {
                session: self,
                index: 0,
                len: unsafe { raw::$count(self.handle.as_ptr()) },
                fetch: |session, index| {
                    let view = unsafe { raw::$at(session.handle.as_ptr(), index) };
                    #[allow(clippy::redundant_closure_call)]
                    ($convert)(view)
                },
            }
        }
    };
}

/// A live WireLab lab. Not `Send`, not `Sync`, closed on drop.
pub struct Session {
    handle: NonNull<raw::wirelab_session>,
    _not_send: PhantomData<*const ()>,
}

impl Session {
    /// `None` when the library speaks a different ABI than this binding was
    /// written against, which is the only failure the C side reports here.
    pub fn open() -> Option<Self> {
        let handle = unsafe { raw::wirelab_session_open(WIRELAB_FFI_ABI_VERSION) };
        NonNull::new(handle).map(|handle| Self {
            handle,
            _not_send: PhantomData,
        })
    }

    /// Returns the accumulated change bits and clears them.
    pub fn take_dirty(&mut self) -> Dirty {
        Dirty(unsafe { raw::wirelab_session_take_dirty(self.handle.as_ptr()) })
    }

    pub fn status_message(&self) -> &str {
        borrowed(unsafe { raw::wirelab_session_status_message(self.handle.as_ptr()) })
    }

    // ---- topology --------------------------------------------------------

    pub fn has_topology(&self) -> bool {
        unsafe { raw::wirelab_topology_loaded(self.handle.as_ptr()) }
    }

    pub fn topology_name(&self) -> &str {
        borrowed(unsafe { raw::wirelab_topology_name(self.handle.as_ptr()) })
    }

    rows!(
        nodes,
        Node<'_>,
        wirelab_topology_node_count,
        wirelab_topology_node_at,
        |view: raw::NodeView| Node {
            id: borrowed(view.id),
            node_type: NodeType::from(view.node_type),
            x: view.x,
            y: view.y,
        }
    );

    rows!(
        links,
        Link<'_>,
        wirelab_topology_link_count,
        wirelab_topology_link_at,
        |view: raw::LinkView| Link {
            from: borrowed(view.from),
            to: borrowed(view.to),
            latency_ms: view.latency_ms,
        }
    );

    pub fn open_topology(&mut self, path: &str) {
        let path = cstring(path);
        unsafe { raw::wirelab_topology_open(self.handle.as_ptr(), path.as_ptr()) }
    }

    pub fn save_topology(&mut self, path: &str) {
        let path = cstring(path);
        unsafe { raw::wirelab_topology_save(self.handle.as_ptr(), path.as_ptr()) }
    }

    pub fn add_node(&mut self, id: &str, node_type: NodeType) {
        let id = cstring(id);
        let label = cstring(match node_type {
            NodeType::Switch => "switch",
            NodeType::Host => "host",
        });
        unsafe { raw::wirelab_topology_add_node(self.handle.as_ptr(), id.as_ptr(), label.as_ptr()) }
    }

    pub fn add_link(&mut self, from: &str, to: &str, latency_ms: i32) {
        let from = cstring(from);
        let to = cstring(to);
        unsafe {
            raw::wirelab_topology_add_link(
                self.handle.as_ptr(),
                from.as_ptr(),
                to.as_ptr(),
                latency_ms,
            )
        }
    }

    pub fn remove_selected(&mut self) {
        unsafe { raw::wirelab_topology_remove_selected(self.handle.as_ptr()) }
    }

    // ---- selection -------------------------------------------------------

    pub fn selection_kind(&self) -> SelectionKind {
        SelectionKind::from(unsafe { raw::wirelab_selection_kind_of(self.handle.as_ptr()) })
    }

    pub fn selected_id(&self) -> &str {
        borrowed(unsafe { raw::wirelab_selected_id(self.handle.as_ptr()) })
    }

    pub fn selected_summary(&self) -> &str {
        borrowed(unsafe { raw::wirelab_selected_summary(self.handle.as_ptr()) })
    }

    pub fn select_node(&mut self, id: &str) {
        let id = cstring(id);
        unsafe { raw::wirelab_select_node(self.handle.as_ptr(), id.as_ptr()) }
    }

    pub fn select_link(&mut self, from: &str, to: &str) {
        let from = cstring(from);
        let to = cstring(to);
        unsafe { raw::wirelab_select_link(self.handle.as_ptr(), from.as_ptr(), to.as_ptr()) }
    }

    pub fn clear_selection(&mut self) {
        unsafe { raw::wirelab_clear_selection(self.handle.as_ptr()) }
    }

    // ---- faults ----------------------------------------------------------

    rows!(
        faults,
        Fault<'_>,
        wirelab_fault_count,
        wirelab_fault_at,
        |view: raw::FaultView| Fault {
            first: borrowed(view.first),
            second: borrowed(view.second),
            latency_ms: view.latency_ms,
            loss_percent: view.loss_percent,
            blackhole: view.blackhole,
            is_link: view.is_link,
        }
    );

    pub fn apply_selected_fault(&mut self, latency_ms: i32, loss_percent: f64, blackhole: bool) {
        unsafe {
            raw::wirelab_fault_apply_selected(
                self.handle.as_ptr(),
                latency_ms,
                loss_percent,
                blackhole,
            )
        }
    }

    pub fn clear_fault(&mut self, first: &str, second: &str) {
        let first = cstring(first);
        let second = cstring(second);
        unsafe { raw::wirelab_fault_clear(self.handle.as_ptr(), first.as_ptr(), second.as_ptr()) }
    }

    // ---- policies --------------------------------------------------------

    rows!(
        policies,
        Policy<'_>,
        wirelab_policy_count,
        wirelab_policy_at,
        |view: raw::PolicyView| Policy {
            name: borrowed(view.name),
            anomaly_type: AnomalyType::from(view.anomaly_type),
            action: PolicyAction::from(view.action),
            enabled: view.enabled,
            rate_limit_packets_per_second: view.rate_limit_packets_per_second,
            hits: view.hits,
        }
    );

    rows!(
        policy_actions,
        PolicyActionRow<'_>,
        wirelab_policy_action_count,
        wirelab_policy_action_at,
        |view: raw::PolicyActionView| PolicyActionRow {
            sequence: view.sequence,
            rule: borrowed(view.rule),
            anomaly_type: AnomalyType::from(view.anomaly_type),
            action: PolicyAction::from(view.action),
            port: borrowed(view.port),
            outcome: EnforcementOutcome::from(view.outcome),
            detail: borrowed(view.detail),
        }
    );

    rows!(
        enforced_ports,
        EnforcedPort<'_>,
        wirelab_enforced_port_count,
        wirelab_enforced_port_at,
        |view: raw::EnforcedPortView| EnforcedPort {
            port: borrowed(view.port),
            rule: borrowed(view.rule),
            kind: EnforcementKind::from(view.kind),
            summary: borrowed(view.summary),
        }
    );

    /// `anomaly_type` and `action` are display names from [`anomaly_type_names`]
    /// and [`policy_action_names`].
    pub fn add_policy(&mut self, name: &str, anomaly_type: &str, action: &str, rate_limit: u64) {
        let name = cstring(name);
        let anomaly_type = cstring(anomaly_type);
        let action = cstring(action);
        unsafe {
            raw::wirelab_policy_add(
                self.handle.as_ptr(),
                name.as_ptr(),
                anomaly_type.as_ptr(),
                action.as_ptr(),
                rate_limit,
            )
        }
    }

    pub fn remove_policy(&mut self, name: &str) {
        let name = cstring(name);
        unsafe { raw::wirelab_policy_remove(self.handle.as_ptr(), name.as_ptr()) }
    }

    pub fn set_policy_enabled(&mut self, name: &str, enabled: bool) {
        let name = cstring(name);
        unsafe { raw::wirelab_policy_set_enabled(self.handle.as_ptr(), name.as_ptr(), enabled) }
    }

    pub fn release_enforcement(&mut self, port_id: &str) {
        let port_id = cstring(port_id);
        unsafe { raw::wirelab_enforcement_release(self.handle.as_ptr(), port_id.as_ptr()) }
    }

    // ---- traffic ---------------------------------------------------------

    pub fn traffic_running(&self) -> bool {
        unsafe { raw::wirelab_traffic_running(self.handle.as_ptr()) }
    }

    pub fn active_backend(&self) -> &str {
        borrowed(unsafe { raw::wirelab_active_backend(self.handle.as_ptr()) })
    }

    pub fn traffic_result(&self) -> &str {
        borrowed(unsafe { raw::wirelab_traffic_result(self.handle.as_ptr()) })
    }

    /// The one row type that crosses as a contiguous span rather than per-row:
    /// it is plain data on both sides, so there is nothing to convert.
    pub fn metrics_history(&self) -> &[MetricSample] {
        let mut count = 0usize;
        let pointer = unsafe { raw::wirelab_metrics_history(self.handle.as_ptr(), &mut count) };
        if pointer.is_null() || count == 0 {
            return &[];
        }
        // SAFETY: the span is session-owned and lives until the next mutation,
        // which &self prevents for the duration of the borrow.
        unsafe { std::slice::from_raw_parts(pointer, count) }
    }

    rows!(
        mac_table,
        MacEntry<'_>,
        wirelab_mac_table_count,
        wirelab_mac_table_at,
        |view: raw::MacTableView| MacEntry {
            mac: borrowed(view.mac),
            port: borrowed(view.port),
        }
    );

    rows!(
        port_states,
        PortState<'_>,
        wirelab_port_state_count,
        wirelab_port_state_at,
        |view: raw::PortStateView| PortState {
            id: borrowed(view.id),
            enforced: view.enforced,
            received: view.received,
            forwarded: view.forwarded,
            dropped: view.dropped,
        }
    );

    rows!(
        packets,
        Packet<'_>,
        wirelab_packet_count,
        wirelab_packet_at,
        |view: raw::PacketView| Packet {
            source_mac: borrowed(view.source_mac),
            destination_mac: borrowed(view.destination_mac),
            source_ip: borrowed(view.source_ip),
            destination_ip: borrowed(view.destination_ip),
            ingress: borrowed(view.ingress),
            protocol: view.protocol,
            destination_port: view.destination_port,
            bytes: view.bytes,
            classification: Classification::from(view.classification),
            validity: Validity::from(view.validity),
        }
    );

    rows!(
        anomalies,
        Anomaly<'_>,
        wirelab_anomaly_count,
        wirelab_anomaly_at,
        |view: raw::AnomalyView| Anomaly {
            anomaly_type: AnomalyType::from(view.anomaly_type),
            source_mac: borrowed(view.source_mac),
            source_ip: borrowed(view.source_ip),
            ingress_port: view.ingress_port,
            observed: view.observed,
            threshold: view.threshold,
        }
    );

    pub fn start_traffic(
        &mut self,
        scenario: &str,
        packets_per_tick: i32,
        frame_size: i32,
        seed: u64,
        backend: &str,
    ) {
        let scenario = cstring(scenario);
        let backend = cstring(backend);
        unsafe {
            raw::wirelab_traffic_start(
                self.handle.as_ptr(),
                scenario.as_ptr(),
                packets_per_tick,
                frame_size,
                seed,
                backend.as_ptr(),
            )
        }
    }

    pub fn stop_traffic(&mut self) {
        unsafe { raw::wirelab_traffic_stop(self.handle.as_ptr()) }
    }

    pub fn step_traffic(&mut self) {
        unsafe { raw::wirelab_traffic_step(self.handle.as_ptr()) }
    }

    // ---- benchmark report ------------------------------------------------

    pub fn report_running(&self) -> bool {
        unsafe { raw::wirelab_report_running(self.handle.as_ptr()) }
    }

    pub fn report_progress(&self) -> f64 {
        unsafe { raw::wirelab_report_progress(self.handle.as_ptr()) }
    }

    pub fn report_stage(&self) -> &str {
        borrowed(unsafe { raw::wirelab_report_stage(self.handle.as_ptr()) })
    }

    pub fn report_export_path(&self) -> &str {
        borrowed(unsafe { raw::wirelab_report_export_path(self.handle.as_ptr()) })
    }

    rows!(
        report_rows,
        ReportRow<'_>,
        wirelab_report_row_count,
        wirelab_report_row_at,
        |view: raw::ReportRowView| ReportRow {
            backend_label: borrowed(view.backend_label),
            backend_id: borrowed(view.backend_id),
            scenario: borrowed(view.scenario),
            packets: view.packets,
            elapsed_ns: view.elapsed_ns,
            packets_per_second: view.packets_per_second,
            goodput_bits_per_second: view.goodput_bits_per_second,
            loss_percent: view.loss_percent,
            latency_p50_ns: view.latency_p50_ns,
            latency_p95_ns: view.latency_p95_ns,
            latency_p99_ns: view.latency_p99_ns,
            host_to_device_ns: view.host_to_device_ns,
            kernel_ns: view.kernel_ns,
            device_to_host_ns: view.device_to_host_ns,
            transfer_inclusive_ns: view.transfer_inclusive_ns,
            queue_wait_ns: view.queue_wait_ns,
            speedup: view.speedup,
        }
    );

    pub fn report_provenance(&self) -> Provenance {
        let view = unsafe { raw::wirelab_report_provenance(self.handle.as_ptr()) };
        let compiled_in_count =
            unsafe { raw::wirelab_report_compiled_in_count(self.handle.as_ptr()) };
        let present_count = unsafe { raw::wirelab_report_present_count(self.handle.as_ptr()) };
        Provenance {
            scenario: owned(view.scenario),
            seed: view.seed,
            packets: view.packets,
            batch_size: view.batch_size,
            frame_size: view.frame_size,
            host_count: view.host_count,
            generator: owned(view.generator),
            version: owned(view.version),
            build_type: owned(view.build_type),
            generated_at: owned(view.generated_at),
            backends_compiled_in: (0..compiled_in_count)
                .map(|index| {
                    owned(unsafe {
                        raw::wirelab_report_compiled_in_at(self.handle.as_ptr(), index)
                    })
                })
                .collect(),
            backends_present: (0..present_count)
                .map(|index| {
                    owned(unsafe { raw::wirelab_report_present_at(self.handle.as_ptr(), index) })
                })
                .collect(),
        }
    }

    pub fn start_report(
        &mut self,
        scenario: &str,
        packets: i32,
        batch_size: i32,
        frame_size: i32,
        seed: i32,
    ) {
        let scenario = cstring(scenario);
        unsafe {
            raw::wirelab_report_start(
                self.handle.as_ptr(),
                scenario.as_ptr(),
                packets,
                batch_size,
                frame_size,
                seed,
            )
        }
    }

    pub fn step_report(&mut self) {
        unsafe { raw::wirelab_report_step(self.handle.as_ptr()) }
    }

    pub fn export_report(&mut self, path: &str) -> bool {
        let path = cstring(path);
        unsafe { raw::wirelab_report_export(self.handle.as_ptr(), path.as_ptr()) }
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        unsafe { raw::wirelab_session_close(self.handle.as_ptr()) }
    }
}

/// An interior NUL cannot reach the C side, so it is cut rather than panicking
/// mid-frame on a value that only ever comes from a text field.
fn cstring(text: &str) -> CString {
    match CString::new(text) {
        Ok(value) => value,
        Err(error) => {
            let position = error.nul_position();
            CString::new(&error.into_vec()[..position]).unwrap_or_default()
        }
    }
}

fn table(
    count: unsafe extern "C" fn() -> usize,
    at: unsafe extern "C" fn(usize) -> raw::WirelabStr,
) -> Vec<String> {
    (0..unsafe { count() })
        .map(|index| owned(unsafe { at(index) }))
        .collect()
}

/// Backends this build has and this machine can actually run.
pub fn backend_names() -> Vec<String> {
    table(raw::wirelab_backend_count, raw::wirelab_backend_name)
}

/// Wire names the traffic and report forms offer.
pub fn scenario_names() -> Vec<String> {
    table(raw::wirelab_scenario_count, raw::wirelab_scenario_name)
}

pub fn anomaly_type_names() -> Vec<String> {
    table(
        raw::wirelab_anomaly_type_count,
        raw::wirelab_anomaly_type_name,
    )
}

pub fn policy_action_names() -> Vec<String> {
    table(
        raw::wirelab_policy_action_name_count,
        raw::wirelab_policy_action_name,
    )
}

pub fn label(node_type: NodeType) -> &'static str {
    borrowed(unsafe { raw::wirelab_node_type_label(node_type as u32) })
}

pub fn classification_label(classification: Classification) -> &'static str {
    borrowed(unsafe { raw::wirelab_classification_label(classification as u32) })
}

pub fn validity_label(validity: Validity) -> &'static str {
    borrowed(unsafe { raw::wirelab_validity_label(validity as u32) })
}

pub fn anomaly_label(anomaly_type: AnomalyType) -> &'static str {
    borrowed(unsafe { raw::wirelab_anomaly_type_label(anomaly_type as u32) })
}

pub fn policy_action_label(action: PolicyAction) -> &'static str {
    borrowed(unsafe { raw::wirelab_policy_action_label(action as u32) })
}

pub fn enforcement_kind_label(kind: EnforcementKind) -> &'static str {
    borrowed(unsafe { raw::wirelab_enforcement_kind_label(kind as u32) })
}

pub fn enforcement_outcome_label(outcome: EnforcementOutcome) -> &'static str {
    borrowed(unsafe { raw::wirelab_enforcement_outcome_label(outcome as u32) })
}

/// Compares every mirrored struct against the layout the C++ compiler actually
/// produced. Hand-written bindings are only trustworthy with this check wired
/// into the test suite: a silent layout drift reads as plausible garbage.
///
/// Returns the list of mismatches; empty means the two agree.
#[must_use]
pub fn layout_mismatches() -> Vec<String> {
    let mut report = raw::AbiLayoutReport::default();
    unsafe { raw::wirelab_abi_layout(&mut report) };

    let mut problems = Vec::new();
    if report.abi_version != WIRELAB_FFI_ABI_VERSION {
        problems.push(format!(
            "abi version: library {} vs binding {WIRELAB_FFI_ABI_VERSION}",
            report.abi_version
        ));
    }

    let mut check = |name: &str, expected: raw::AbiType, size: usize, align: usize| {
        if expected.size as usize != size || expected.align as usize != align {
            problems.push(format!(
                "{name}: library size {} align {} vs binding size {size} align {align}",
                expected.size, expected.align
            ));
        }
    };

    macro_rules! compare {
        ($field:ident, $rust:ty) => {
            check(
                stringify!($field),
                report.$field,
                std::mem::size_of::<$rust>(),
                std::mem::align_of::<$rust>(),
            )
        };
    }

    compare!(str_, raw::WirelabStr);
    compare!(node, raw::NodeView);
    compare!(link, raw::LinkView);
    compare!(metric_sample, raw::MetricSample);
    compare!(mac_table, raw::MacTableView);
    compare!(port_state, raw::PortStateView);
    compare!(packet, raw::PacketView);
    compare!(anomaly, raw::AnomalyView);
    compare!(fault, raw::FaultView);
    compare!(policy, raw::PolicyView);
    compare!(policy_action, raw::PolicyActionView);
    compare!(enforced_port, raw::EnforcedPortView);
    compare!(report_row, raw::ReportRowView);
    compare!(provenance, raw::ProvenanceView);

    problems
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scenario_path() -> String {
        // The crate lives at <repo>/gui, so the scenarios ship one level up.
        format!(
            "{}/../scenarios/security-lab.yaml",
            env!("CARGO_MANIFEST_DIR")
        )
    }

    /// The one test that makes hand-written bindings defensible: it asks the
    /// C++ compiler what it actually produced and compares it field by field.
    /// Everything else in this file is only meaningful if this passes.
    #[test]
    fn mirrors_match_the_library_layout() {
        let problems = layout_mismatches();
        assert!(problems.is_empty(), "ABI layout drift: {problems:#?}");
    }

    #[test]
    fn opens_a_session_and_reads_the_static_tables() {
        let session = Session::open().expect("ABI mismatch");
        drop(session);

        let backends = backend_names();
        assert_eq!(backends.first().map(String::as_str), Some("CPU"));
        assert_eq!(scenario_names().len(), 7);
        assert!(scenario_names().iter().any(|name| name == "mixed-traffic"));
        assert_eq!(anomaly_type_names().len(), 7);
        assert_eq!(anomaly_type_names()[0], "Broadcast storm");
        // Weakest action first, the order the policy form offers.
        assert_eq!(
            policy_action_names().first().map(String::as_str),
            Some("Alert only")
        );
        assert_eq!(label(NodeType::Switch), "switch");
        assert_eq!(classification_label(Classification::Broadcast), "Broadcast");
        assert_eq!(anomaly_label(AnomalyType::PortScan), "Port scan");
    }

    #[test]
    fn drives_a_topology_traffic_and_fault_workflow() {
        let mut session = Session::open().expect("ABI mismatch");
        // The constructor publishes the initial policy model; drain it first.
        let _ = session.take_dirty();

        session.open_topology(&scenario_path());
        let opened = session.take_dirty();
        assert!(
            opened.contains(Dirty::TOPOLOGY),
            "status: {}",
            session.status_message()
        );
        assert!(opened.contains(Dirty::STATUS));
        // Draining is destructive: a change is never reported twice.
        assert!(session.take_dirty().is_empty());

        assert!(session.has_topology());
        assert_eq!(session.topology_name(), "security-lab");
        assert_eq!(session.nodes().len(), 4);
        assert_eq!(session.links().len(), 3);
        assert!(
            session
                .nodes()
                .any(|node| node.node_type == NodeType::Switch)
        );
        assert!(session.nodes().all(|node| (0.0..=1.0).contains(&node.x)));

        session.select_node("client-a");
        assert_eq!(session.selection_kind(), SelectionKind::Node);
        assert_eq!(session.selected_id(), "client-a");

        session.apply_selected_fault(25, 0.0, false);
        assert_eq!(session.faults().len(), 1);
        let fault = session.faults().next().expect("one fault");
        assert_eq!(fault.first, "client-a");
        assert_eq!(fault.latency_ms, 25);
        assert!(!fault.is_link);

        session.start_traffic("mixed-traffic", 64, 128, 42, "CPU");
        assert!(
            session.traffic_running(),
            "status: {}",
            session.status_message()
        );
        session.step_traffic();
        session.stop_traffic();

        assert_eq!(session.metrics_history().len(), 1);
        assert_eq!(session.metrics_history()[0].sequence, 1);
        assert!(session.packets().len() > 0);
        assert!(session.mac_table().len() > 0);
        assert_eq!(session.port_states().len(), 3);
        assert!(session.traffic_result().contains("64 packets generated"));

        let packet = session.packets().next().expect("one packet");
        assert_eq!(
            packet.source_mac.len(),
            17,
            "a MAC reads as aa:bb:cc:dd:ee:ff"
        );
        assert!(!packet.source_ip.is_empty());

        session.clear_fault("client-a", "");
        assert_eq!(session.faults().len(), 0);
    }

    #[test]
    fn rejects_a_policy_it_cannot_parse() {
        let mut session = Session::open().expect("ABI mismatch");
        session.add_policy("storm-guard", "Broadcast storm", "Quarantine", 0);
        assert_eq!(session.policies().len(), 1);
        let rule = session.policies().next().expect("one rule");
        assert_eq!(rule.anomaly_type, AnomalyType::BroadcastStorm);
        assert_eq!(rule.action, PolicyAction::Quarantine);
        assert!(rule.enabled);

        session.set_policy_enabled("storm-guard", false);
        assert!(!session.policies().next().expect("one rule").enabled);

        session.add_policy("bogus", "Not an anomaly", "Quarantine", 0);
        assert_eq!(session.policies().len(), 1);
        assert!(session.status_message().contains("Unknown anomaly type"));
    }

    #[test]
    fn runs_a_report_to_completion() {
        let mut session = Session::open().expect("ABI mismatch");
        session.start_report("port-scan", 256, 32, 64, 3);
        assert!(
            session.report_running(),
            "status: {}",
            session.status_message()
        );

        let mut steps = 0;
        while session.report_running() && steps < 10_000 {
            session.step_report();
            steps += 1;
        }
        assert!(!session.report_running());
        assert_eq!(session.report_progress(), 1.0);
        assert_eq!(session.report_rows().len(), backend_names().len());

        let row = session.report_rows().next().expect("one row");
        assert_eq!(row.backend_label, "CPU");
        assert_eq!(row.backend_id, "cpu");
        assert_eq!(row.packets, 256);
        assert!(row.packets_per_second > 0.0);

        let provenance = session.report_provenance();
        assert_eq!(provenance.scenario, "port-scan");
        assert_eq!(provenance.seed, 3);
        assert!(provenance.generated_at.ends_with('Z'));
        assert_eq!(provenance.backends_present, backend_names());

        let base = std::env::temp_dir().join("wirelab-rust-report");
        let json = base.with_extension("json");
        let csv = base.with_extension("csv");
        let _ = std::fs::remove_file(&json);
        let _ = std::fs::remove_file(&csv);
        assert!(session.export_report(&json.display().to_string()));
        assert!(json.exists() && csv.exists());
        let _ = std::fs::remove_file(&json);
        let _ = std::fs::remove_file(&csv);
    }

    /// An interior NUL can only arrive from a text field, and cutting it is
    /// preferable to panicking in the middle of a frame.
    #[test]
    fn truncates_a_string_with_an_interior_nul() {
        let value = cstring("client\0-a");
        assert_eq!(value.to_bytes(), b"client");
    }
}
