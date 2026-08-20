//! Hand-written mirrors of `include/wirelab/wirelab_ffi.h`.
//!
//! Nothing in this module is safe to call. The rules the C header states are
//! upheld by [`crate::ffi::Session`], not here:
//!
//! * a session belongs to exactly one thread;
//! * every `WirelabStr` and every string inside a view is borrowed from
//!   session-owned storage and dies at the next mutating call;
//! * indices are checked on the C side and answer with a zeroed view rather
//!   than trapping, so an out-of-range read is wrong but not unsound.
//!
//! Layout is not assumed: [`super::layout_matches`] compares every struct here
//! against what the C++ compiler actually produced.

#![allow(non_camel_case_types)]

use std::ffi::c_char;
use std::os::raw::c_void;

pub const WIRELAB_FFI_ABI_VERSION: u32 = 1;

pub const WIRELAB_DIRTY_TOPOLOGY: u32 = 1 << 0;
pub const WIRELAB_DIRTY_SELECTION: u32 = 1 << 1;
pub const WIRELAB_DIRTY_STATUS: u32 = 1 << 2;
pub const WIRELAB_DIRTY_TRAFFIC_STATE: u32 = 1 << 3;
pub const WIRELAB_DIRTY_TELEMETRY: u32 = 1 << 4;
pub const WIRELAB_DIRTY_FAULTS: u32 = 1 << 5;
pub const WIRELAB_DIRTY_POLICIES: u32 = 1 << 6;
pub const WIRELAB_DIRTY_REPORT: u32 = 1 << 7;

/// Opaque; only ever held behind a pointer.
#[repr(C)]
pub struct wirelab_session {
    _private: [u8; 0],
    _not_send: core::marker::PhantomData<*const c_void>,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WirelabStr {
    pub ptr: *const c_char,
    pub len: usize,
}

impl Default for WirelabStr {
    fn default() -> Self {
        Self {
            ptr: std::ptr::null(),
            len: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct NodeView {
    pub id: WirelabStr,
    pub node_type: u32,
    pub x: f64,
    pub y: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct LinkView {
    pub from: WirelabStr,
    pub to: WirelabStr,
    pub latency_ms: i64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MetricSample {
    pub sequence: u64,
    pub throughput_mbps: f64,
    pub latency_ms: f64,
    pub loss_percent: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MacTableView {
    pub mac: WirelabStr,
    pub port: WirelabStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PortStateView {
    pub id: WirelabStr,
    pub enforced: bool,
    pub received: u64,
    pub forwarded: u64,
    pub dropped: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PacketView {
    pub source_mac: WirelabStr,
    pub destination_mac: WirelabStr,
    pub source_ip: WirelabStr,
    pub destination_ip: WirelabStr,
    pub ingress: WirelabStr,
    pub protocol: u8,
    pub destination_port: u16,
    pub bytes: u16,
    pub classification: u32,
    pub validity: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AnomalyView {
    pub anomaly_type: u32,
    pub source_mac: WirelabStr,
    pub source_ip: WirelabStr,
    pub ingress_port: u32,
    pub observed: u64,
    pub threshold: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct FaultView {
    pub first: WirelabStr,
    pub second: WirelabStr,
    pub latency_ms: i64,
    pub loss_percent: f64,
    pub blackhole: bool,
    pub is_link: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PolicyView {
    pub name: WirelabStr,
    pub anomaly_type: u32,
    pub action: u32,
    pub enabled: bool,
    pub rate_limit_packets_per_second: u64,
    pub hits: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PolicyActionView {
    pub sequence: u64,
    pub rule: WirelabStr,
    pub anomaly_type: u32,
    pub action: u32,
    pub port: WirelabStr,
    pub outcome: u32,
    pub detail: WirelabStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct EnforcedPortView {
    pub port: WirelabStr,
    pub rule: WirelabStr,
    pub kind: u32,
    pub summary: WirelabStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct ReportRowView {
    pub backend_label: WirelabStr,
    pub backend_id: WirelabStr,
    pub scenario: WirelabStr,
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

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct ProvenanceView {
    pub scenario: WirelabStr,
    pub seed: u64,
    pub packets: u64,
    pub batch_size: u64,
    pub frame_size: u64,
    pub host_count: u64,
    pub generator: WirelabStr,
    pub version: WirelabStr,
    pub build_type: WirelabStr,
    pub generated_at: WirelabStr,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AbiType {
    pub size: u32,
    pub align: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AbiLayoutReport {
    pub abi_version: u32,
    pub str_: AbiType,
    pub node: AbiType,
    pub link: AbiType,
    pub metric_sample: AbiType,
    pub mac_table: AbiType,
    pub port_state: AbiType,
    pub packet: AbiType,
    pub anomaly: AbiType,
    pub fault: AbiType,
    pub policy: AbiType,
    pub policy_action: AbiType,
    pub enforced_port: AbiType,
    pub report_row: AbiType,
    pub provenance: AbiType,
}

unsafe extern "C" {
    pub fn wirelab_ffi_abi_version() -> u32;
    pub fn wirelab_session_open(abi_version: u32) -> *mut wirelab_session;
    pub fn wirelab_session_close(session: *mut wirelab_session);
    pub fn wirelab_session_take_dirty(session: *mut wirelab_session) -> u32;
    pub fn wirelab_session_status_message(session: *const wirelab_session) -> WirelabStr;

    pub fn wirelab_topology_loaded(session: *const wirelab_session) -> bool;
    pub fn wirelab_topology_name(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_topology_node_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_topology_node_at(session: *const wirelab_session, index: usize) -> NodeView;
    pub fn wirelab_topology_link_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_topology_link_at(session: *const wirelab_session, index: usize) -> LinkView;
    pub fn wirelab_topology_open(session: *mut wirelab_session, path: *const c_char);
    pub fn wirelab_topology_save(session: *mut wirelab_session, path: *const c_char);
    pub fn wirelab_topology_add_node(
        session: *mut wirelab_session,
        id: *const c_char,
        node_type: *const c_char,
    );
    pub fn wirelab_topology_add_link(
        session: *mut wirelab_session,
        from: *const c_char,
        to: *const c_char,
        latency_ms: i32,
    );
    pub fn wirelab_topology_remove_selected(session: *mut wirelab_session);

    pub fn wirelab_selection_kind_of(session: *const wirelab_session) -> u32;
    pub fn wirelab_selected_id(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_selected_summary(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_select_node(session: *mut wirelab_session, id: *const c_char);
    pub fn wirelab_select_link(
        session: *mut wirelab_session,
        from: *const c_char,
        to: *const c_char,
    );
    pub fn wirelab_clear_selection(session: *mut wirelab_session);

    pub fn wirelab_fault_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_fault_at(session: *const wirelab_session, index: usize) -> FaultView;
    pub fn wirelab_fault_apply_selected(
        session: *mut wirelab_session,
        latency_ms: i32,
        loss_percent: f64,
        blackhole: bool,
    );
    pub fn wirelab_fault_clear(
        session: *mut wirelab_session,
        first_endpoint: *const c_char,
        second_endpoint: *const c_char,
    );

    pub fn wirelab_policy_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_policy_at(session: *const wirelab_session, index: usize) -> PolicyView;
    pub fn wirelab_policy_action_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_policy_action_at(
        session: *const wirelab_session,
        index: usize,
    ) -> PolicyActionView;
    pub fn wirelab_enforced_port_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_enforced_port_at(
        session: *const wirelab_session,
        index: usize,
    ) -> EnforcedPortView;
    pub fn wirelab_policy_add(
        session: *mut wirelab_session,
        name: *const c_char,
        anomaly_type: *const c_char,
        action: *const c_char,
        rate_limit_packets_per_second: u64,
    );
    pub fn wirelab_policy_remove(session: *mut wirelab_session, name: *const c_char);
    pub fn wirelab_policy_set_enabled(
        session: *mut wirelab_session,
        name: *const c_char,
        enabled: bool,
    );
    pub fn wirelab_enforcement_release(session: *mut wirelab_session, port_id: *const c_char);

    pub fn wirelab_traffic_running(session: *const wirelab_session) -> bool;
    pub fn wirelab_active_backend(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_traffic_result(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_metrics_history(
        session: *const wirelab_session,
        out_count: *mut usize,
    ) -> *const MetricSample;
    pub fn wirelab_mac_table_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_mac_table_at(session: *const wirelab_session, index: usize) -> MacTableView;
    pub fn wirelab_port_state_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_port_state_at(session: *const wirelab_session, index: usize) -> PortStateView;
    pub fn wirelab_packet_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_packet_at(session: *const wirelab_session, index: usize) -> PacketView;
    pub fn wirelab_anomaly_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_anomaly_at(session: *const wirelab_session, index: usize) -> AnomalyView;
    pub fn wirelab_traffic_start(
        session: *mut wirelab_session,
        scenario: *const c_char,
        packets_per_tick: i32,
        frame_size: i32,
        seed: u64,
        backend: *const c_char,
    );
    pub fn wirelab_traffic_stop(session: *mut wirelab_session);
    pub fn wirelab_traffic_step(session: *mut wirelab_session);

    pub fn wirelab_report_running(session: *const wirelab_session) -> bool;
    pub fn wirelab_report_progress(session: *const wirelab_session) -> f64;
    pub fn wirelab_report_stage(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_report_export_path(session: *const wirelab_session) -> WirelabStr;
    pub fn wirelab_report_row_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_report_row_at(session: *const wirelab_session, index: usize) -> ReportRowView;
    pub fn wirelab_report_provenance(session: *const wirelab_session) -> ProvenanceView;
    pub fn wirelab_report_compiled_in_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_report_compiled_in_at(
        session: *const wirelab_session,
        index: usize,
    ) -> WirelabStr;
    pub fn wirelab_report_present_count(session: *const wirelab_session) -> usize;
    pub fn wirelab_report_present_at(session: *const wirelab_session, index: usize) -> WirelabStr;
    pub fn wirelab_report_start(
        session: *mut wirelab_session,
        scenario: *const c_char,
        packets: i32,
        batch_size: i32,
        frame_size: i32,
        seed: i32,
    );
    pub fn wirelab_report_step(session: *mut wirelab_session);
    pub fn wirelab_report_export(session: *mut wirelab_session, path: *const c_char) -> bool;

    pub fn wirelab_backend_count() -> usize;
    pub fn wirelab_backend_name(index: usize) -> WirelabStr;
    pub fn wirelab_scenario_count() -> usize;
    pub fn wirelab_scenario_name(index: usize) -> WirelabStr;
    pub fn wirelab_anomaly_type_count() -> usize;
    pub fn wirelab_anomaly_type_name(index: usize) -> WirelabStr;
    pub fn wirelab_policy_action_name_count() -> usize;
    pub fn wirelab_policy_action_name(index: usize) -> WirelabStr;

    pub fn wirelab_node_type_label(node_type: u32) -> WirelabStr;
    pub fn wirelab_classification_label(classification: u32) -> WirelabStr;
    pub fn wirelab_validity_label(validity: u32) -> WirelabStr;
    pub fn wirelab_anomaly_type_label(anomaly_type: u32) -> WirelabStr;
    pub fn wirelab_policy_action_label(action: u32) -> WirelabStr;
    pub fn wirelab_enforcement_kind_label(kind: u32) -> WirelabStr;
    pub fn wirelab_enforcement_outcome_label(outcome: u32) -> WirelabStr;

    pub fn wirelab_abi_layout(out: *mut AbiLayoutReport);
}
