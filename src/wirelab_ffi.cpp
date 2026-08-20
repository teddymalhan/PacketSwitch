#include "wirelab/wirelab_ffi.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "wirelab/anomaly_detector.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/policy_enforcer.hpp"
#include "wirelab/policy_engine.hpp"
#include "wirelab/session.hpp"
#include "wirelab/topology.hpp"

// The C handle is a wrapper rather than an alias for wirelab::Session because
// the boundary owes the caller two things the core does not model: a place to
// park an escaped exception, and a place to hold dirty bits raised outside a
// take_dirty() the core knows about.
struct wirelab_session
{
  wirelab::Session session;
  // An exception escaping the core has to surface somewhere the frontend already
  // looks, so it is reported through the same status channel a rejected command uses.
  std::string last_error;
  uint32_t extra_dirty = 0;
};

namespace
{
  // ---- enum bridging -----------------------------------------------------
  // These are the guard rails for the whole enum surface. Every uint32_t field
  // in a view struct is one of these enums memcpy'd across the boundary, so a
  // single reordered enumerator would silently relabel rows instead of failing.
  // If one of these fires, the header and the core have drifted apart.

  constexpr uint32_t as_u32(wirelab::TopologyNodeType value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::SelectionKind value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::PacketClassification value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::PacketValidity value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::AnomalyType value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::PolicyAction value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::EnforcementKind value) noexcept
  {
    return static_cast<uint32_t>(value);
  }
  constexpr uint32_t as_u32(wirelab::EnforcementOutcome value) noexcept
  {
    return static_cast<uint32_t>(value);
  }

  static_assert(as_u32(wirelab::TopologyNodeType::Host) == WIRELAB_NODE_HOST, "TopologyNodeType::Host drifted");
  static_assert(as_u32(wirelab::TopologyNodeType::Switch) == WIRELAB_NODE_SWITCH, "TopologyNodeType::Switch drifted");

  static_assert(as_u32(wirelab::SelectionKind::None) == WIRELAB_SELECTION_NONE, "SelectionKind::None drifted");
  static_assert(as_u32(wirelab::SelectionKind::Node) == WIRELAB_SELECTION_NODE, "SelectionKind::Node drifted");
  static_assert(as_u32(wirelab::SelectionKind::Link) == WIRELAB_SELECTION_LINK, "SelectionKind::Link drifted");

  static_assert(
      as_u32(wirelab::PacketClassification::Malformed) == WIRELAB_CLASSIFICATION_MALFORMED,
      "PacketClassification::Malformed drifted");
  static_assert(
      as_u32(wirelab::PacketClassification::Broadcast) == WIRELAB_CLASSIFICATION_BROADCAST,
      "PacketClassification::Broadcast drifted");
  static_assert(
      as_u32(wirelab::PacketClassification::UnknownUnicast) == WIRELAB_CLASSIFICATION_UNKNOWN_UNICAST,
      "PacketClassification::UnknownUnicast drifted");
  static_assert(
      as_u32(wirelab::PacketClassification::KnownUnicast) == WIRELAB_CLASSIFICATION_KNOWN_UNICAST,
      "PacketClassification::KnownUnicast drifted");

  static_assert(as_u32(wirelab::PacketValidity::Valid) == WIRELAB_VALIDITY_VALID, "PacketValidity::Valid drifted");
  static_assert(
      as_u32(wirelab::PacketValidity::MalformedEthernet) == WIRELAB_VALIDITY_MALFORMED_ETHERNET,
      "PacketValidity::MalformedEthernet drifted");
  static_assert(
      as_u32(wirelab::PacketValidity::MalformedIpv4) == WIRELAB_VALIDITY_MALFORMED_IPV4,
      "PacketValidity::MalformedIpv4 drifted");
  static_assert(
      as_u32(wirelab::PacketValidity::MalformedTransport) == WIRELAB_VALIDITY_MALFORMED_TRANSPORT,
      "PacketValidity::MalformedTransport drifted");

  static_assert(
      as_u32(wirelab::AnomalyType::BroadcastStorm) == WIRELAB_ANOMALY_BROADCAST_STORM,
      "AnomalyType::BroadcastStorm drifted");
  static_assert(as_u32(wirelab::AnomalyType::MacFlap) == WIRELAB_ANOMALY_MAC_FLAP, "AnomalyType::MacFlap drifted");
  static_assert(
      as_u32(wirelab::AnomalyType::UnknownUnicastFlood) == WIRELAB_ANOMALY_UNKNOWN_UNICAST_FLOOD,
      "AnomalyType::UnknownUnicastFlood drifted");
  static_assert(as_u32(wirelab::AnomalyType::UdpFlood) == WIRELAB_ANOMALY_UDP_FLOOD, "AnomalyType::UdpFlood drifted");
  static_assert(as_u32(wirelab::AnomalyType::PortScan) == WIRELAB_ANOMALY_PORT_SCAN, "AnomalyType::PortScan drifted");
  static_assert(as_u32(wirelab::AnomalyType::HotTalker) == WIRELAB_ANOMALY_HOT_TALKER, "AnomalyType::HotTalker drifted");
  static_assert(
      as_u32(wirelab::AnomalyType::MalformedFrame) == WIRELAB_ANOMALY_MALFORMED_FRAME,
      "AnomalyType::MalformedFrame drifted");

  static_assert(as_u32(wirelab::PolicyAction::Allow) == WIRELAB_POLICY_ALLOW, "PolicyAction::Allow drifted");
  static_assert(as_u32(wirelab::PolicyAction::Drop) == WIRELAB_POLICY_DROP, "PolicyAction::Drop drifted");
  static_assert(as_u32(wirelab::PolicyAction::Mirror) == WIRELAB_POLICY_MIRROR, "PolicyAction::Mirror drifted");
  static_assert(as_u32(wirelab::PolicyAction::RateLimit) == WIRELAB_POLICY_RATE_LIMIT, "PolicyAction::RateLimit drifted");
  static_assert(
      as_u32(wirelab::PolicyAction::Quarantine) == WIRELAB_POLICY_QUARANTINE,
      "PolicyAction::Quarantine drifted");
  static_assert(as_u32(wirelab::PolicyAction::AlertOnly) == WIRELAB_POLICY_ALERT_ONLY, "PolicyAction::AlertOnly drifted");

  static_assert(as_u32(wirelab::EnforcementKind::None) == WIRELAB_ENFORCEMENT_NONE, "EnforcementKind::None drifted");
  static_assert(
      as_u32(wirelab::EnforcementKind::RateLimit) == WIRELAB_ENFORCEMENT_RATE_LIMIT,
      "EnforcementKind::RateLimit drifted");
  static_assert(
      as_u32(wirelab::EnforcementKind::Blackhole) == WIRELAB_ENFORCEMENT_BLACKHOLE,
      "EnforcementKind::Blackhole drifted");
  static_assert(
      as_u32(wirelab::EnforcementKind::Isolate) == WIRELAB_ENFORCEMENT_ISOLATE,
      "EnforcementKind::Isolate drifted");

  static_assert(as_u32(wirelab::EnforcementOutcome::Applied) == WIRELAB_OUTCOME_APPLIED, "EnforcementOutcome::Applied drifted");
  static_assert(
      as_u32(wirelab::EnforcementOutcome::Extended) == WIRELAB_OUTCOME_EXTENDED,
      "EnforcementOutcome::Extended drifted");
  static_assert(
      as_u32(wirelab::EnforcementOutcome::Released) == WIRELAB_OUTCOME_RELEASED,
      "EnforcementOutcome::Released drifted");
  static_assert(as_u32(wirelab::EnforcementOutcome::Skipped) == WIRELAB_OUTCOME_SKIPPED, "EnforcementOutcome::Skipped drifted");
  static_assert(
      as_u32(wirelab::EnforcementOutcome::UnknownPort) == WIRELAB_OUTCOME_UNKNOWN_PORT,
      "EnforcementOutcome::UnknownPort drifted");
  static_assert(
      as_u32(wirelab::EnforcementOutcome::Rejected) == WIRELAB_OUTCOME_REJECTED,
      "EnforcementOutcome::Rejected drifted");

  // ---- metric span layout ------------------------------------------------
  // Every other row is copied field by field, so a layout mismatch there is a
  // compile error at worst. The metric history is the one place the shim hands
  // over raw core memory reinterpreted as a C struct, where a disagreement
  // would be silent and would corrupt every chart the frontend draws.

  static_assert(std::is_standard_layout<wirelab::MetricSample>::value, "MetricSample must be standard layout to span");
  static_assert(std::is_trivially_copyable<wirelab::MetricSample>::value, "MetricSample must be trivially copyable to span");
  static_assert(sizeof(wirelab::MetricSample) == sizeof(wirelab_metric_sample), "MetricSample size mismatch");
  static_assert(alignof(wirelab::MetricSample) == alignof(wirelab_metric_sample), "MetricSample alignment mismatch");
  static_assert(
      offsetof(wirelab::MetricSample, sequence) == offsetof(wirelab_metric_sample, sequence),
      "MetricSample::sequence offset mismatch");
  static_assert(
      offsetof(wirelab::MetricSample, throughput_mbps) == offsetof(wirelab_metric_sample, throughput_mbps),
      "MetricSample::throughput_mbps offset mismatch");
  static_assert(
      offsetof(wirelab::MetricSample, latency_ms) == offsetof(wirelab_metric_sample, latency_ms),
      "MetricSample::latency_ms offset mismatch");
  static_assert(
      offsetof(wirelab::MetricSample, loss_percent) == offsetof(wirelab_metric_sample, loss_percent),
      "MetricSample::loss_percent offset mismatch");

  // ---- string borrowing --------------------------------------------------

  // Borrows; it never copies. c_str() is never null, so the view always points
  // at readable storage even when the string is empty.
  wirelab_str to_str(const std::string& text) noexcept
  {
    return wirelab_str{ text.c_str(), text.size() };
  }

  // For the static tables, whose entries are string literals owned by the core.
  wirelab_str to_str(const char* text) noexcept
  {
    if (text == nullptr)
      return wirelab_str{};
    // char_traits::length lowers to the platform strlen; these are short
    // literals measured on every label lookup, so it is worth not hand-rolling.
    return wirelab_str{ text, std::char_traits<char>::length(text) };
  }

  // Commands take std::string by const reference, so a copy is unavoidable
  // there; getters never call this. A null pointer is the empty string.
  std::string arg_string(const char* text)
  {
    return text == nullptr ? std::string() : std::string(text);
  }

  // ---- indexing ----------------------------------------------------------

  // An out-of-range index must not trap: the frontend indexes rows it observed
  // one repaint ago, and the core may have shortened the vector since.
  template <typename Vector>
  const typename Vector::value_type* row_at(const Vector& rows, size_t index) noexcept
  {
    return index < rows.size() ? &rows[index] : nullptr;
  }

  // ---- command guard -----------------------------------------------------

  void record_error(wirelab_session& handle, const char* what) noexcept
  {
    try
    {
      handle.last_error = (what == nullptr) ? std::string("Internal error.") : ("Internal error: " + std::string(what));
    }
    catch (...)
    {
      // Out of memory while reporting out of memory. The dirty bit below still
      // tells the frontend to re-read the status, which will fall through to
      // the core's own message.
      handle.last_error.clear();
    }
    // The frontend only re-reads the status when this bit is raised, so an
    // error that does not set it would never be displayed.
    handle.extra_dirty |= WIRELAB_DIRTY_STATUS;
  }

  // Every command runs through here: a null handle is a no-op, and no exception
  // can reach the C caller.
  template <typename Fn>
  void guard(wirelab_session* handle, Fn&& body)
  {
    if (handle == nullptr)
      return;
    handle->last_error.clear();
    try
    {
      body(handle->session);
    }
    catch (const std::exception& error)
    {
      record_error(*handle, error.what());
    }
    catch (...)
    {
      record_error(*handle, nullptr);
    }
  }

  // The value-returning sibling, for the one command that answers a bool.
  // A throw is a failure to write, which is exactly what false means.
  template <typename Fn>
  bool guard_bool(wirelab_session* handle, Fn&& body)
  {
    if (handle == nullptr)
      return false;
    handle->last_error.clear();
    try
    {
      return body(handle->session);
    }
    catch (const std::exception& error)
    {
      record_error(*handle, error.what());
    }
    catch (...)
    {
      record_error(*handle, nullptr);
    }
    return false;
  }

  // ---- static tables -----------------------------------------------------

  // Session::available_backends() and scenario_names() build their vector on
  // first use, which is the one allocation in the static-table paths. Failing
  // it yields an empty table rather than a terminate, because these entry
  // points have C linkage and cannot be marked noexcept.
  const std::vector<std::string>& backend_table() noexcept
  {
    static const std::vector<std::string> fallback;
    try
    {
      return wirelab::Session::available_backends();
    }
    catch (...)
    {
      return fallback;
    }
  }

  const std::vector<std::string>& scenario_table() noexcept
  {
    static const std::vector<std::string> fallback;
    try
    {
      return wirelab::scenario_names();
    }
    catch (...)
    {
      return fallback;
    }
  }

  constexpr wirelab::AnomalyType kAnomalyOrder[] = {
    wirelab::AnomalyType::BroadcastStorm, wirelab::AnomalyType::MacFlap, wirelab::AnomalyType::UnknownUnicastFlood,
    wirelab::AnomalyType::UdpFlood,       wirelab::AnomalyType::PortScan, wirelab::AnomalyType::HotTalker,
    wirelab::AnomalyType::MalformedFrame
  };
  constexpr size_t kAnomalyOrderCount = sizeof(kAnomalyOrder) / sizeof(kAnomalyOrder[0]);

  // Weakest to strongest, not enum order. This is the order the Qt frontend
  // offered in its policy form and the order a person expects to scan; the
  // enum happens to be ordered the other way round.
  constexpr wirelab::PolicyAction kPolicyActionOrder[] = { wirelab::PolicyAction::AlertOnly,
                                                           wirelab::PolicyAction::Mirror,
                                                           wirelab::PolicyAction::RateLimit,
                                                           wirelab::PolicyAction::Drop,
                                                           wirelab::PolicyAction::Quarantine,
                                                           wirelab::PolicyAction::Allow };
  constexpr size_t kPolicyActionOrderCount = sizeof(kPolicyActionOrder) / sizeof(kPolicyActionOrder[0]);

  // ---- ABI self-check ----------------------------------------------------

  template <typename T>
  constexpr wirelab_abi_type abi_type_of() noexcept
  {
    return wirelab_abi_type{ static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(alignof(T)) };
  }
}  // namespace

// ---- lifecycle -----------------------------------------------------------

uint32_t wirelab_ffi_abi_version(void)
{
  return WIRELAB_FFI_ABI_VERSION;
}

wirelab_session* wirelab_session_open(uint32_t abi_version)
{
  if (abi_version != WIRELAB_FFI_ABI_VERSION)
    return nullptr;
  try
  {
    std::unique_ptr<wirelab_session> handle(new wirelab_session());
    // The Session constructor seeds the default policy set and marks Policies
    // dirty. Draining it here and stashing it means the caller's first
    // take_dirty() still sees the initial state, instead of the bit being lost
    // because it was raised before the caller had a handle.
    handle->extra_dirty = handle->session.take_dirty();
    return handle.release();
  }
  catch (...)
  {
    return nullptr;
  }
}

void wirelab_session_close(wirelab_session* session)
{
  delete session;
}

uint32_t wirelab_session_take_dirty(wirelab_session* session)
{
  if (session == nullptr)
    return 0U;
  const uint32_t bits = session->session.take_dirty() | session->extra_dirty;
  session->extra_dirty = 0U;
  return bits;
}

wirelab_str wirelab_session_status_message(const wirelab_session* session)
{
  if (session == nullptr)
    return wirelab_str{};
  // A shim-level failure shadows the core's message: the core never saw the
  // command, so its status still describes whatever happened before.
  if (!session->last_error.empty())
    return to_str(session->last_error);
  return to_str(session->session.status_message());
}

// ---- topology ------------------------------------------------------------

bool wirelab_topology_loaded(const wirelab_session* session)
{
  return session != nullptr && session->session.has_topology();
}

wirelab_str wirelab_topology_name(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.topology_name());
}

size_t wirelab_topology_node_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.topology_nodes().size();
}

wirelab_node_view wirelab_topology_node_at(const wirelab_session* session, size_t index)
{
  wirelab_node_view view{};
  if (session == nullptr)
    return view;
  const wirelab::NodeRow* row = row_at(session->session.topology_nodes(), index);
  if (row == nullptr)
    return view;
  view.id = to_str(row->id);
  view.type = as_u32(row->type);
  view.x = row->x;
  view.y = row->y;
  return view;
}

size_t wirelab_topology_link_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.topology_links().size();
}

wirelab_link_view wirelab_topology_link_at(const wirelab_session* session, size_t index)
{
  wirelab_link_view view{};
  if (session == nullptr)
    return view;
  const wirelab::LinkRow* row = row_at(session->session.topology_links(), index);
  if (row == nullptr)
    return view;
  view.from = to_str(row->from);
  view.to = to_str(row->to);
  view.latency_ms = row->latency_ms;
  return view;
}

void wirelab_topology_open(wirelab_session* session, const char* path)
{
  guard(session, [path](wirelab::Session& core) { core.open_topology(arg_string(path)); });
}

void wirelab_topology_save(wirelab_session* session, const char* path)
{
  guard(session, [path](wirelab::Session& core) { core.save_topology(arg_string(path)); });
}

void wirelab_topology_add_node(wirelab_session* session, const char* id, const char* type)
{
  guard(session, [id, type](wirelab::Session& core) { core.add_node(arg_string(id), arg_string(type)); });
}

void wirelab_topology_add_link(wirelab_session* session, const char* from, const char* to, int32_t latency_ms)
{
  guard(
      session,
      [from, to, latency_ms](wirelab::Session& core) { core.add_link(arg_string(from), arg_string(to), latency_ms); });
}

void wirelab_topology_remove_selected(wirelab_session* session)
{
  guard(session, [](wirelab::Session& core) { core.remove_selected(); });
}

// ---- selection -----------------------------------------------------------

uint32_t wirelab_selection_kind_of(const wirelab_session* session)
{
  return session == nullptr ? static_cast<uint32_t>(WIRELAB_SELECTION_NONE) : as_u32(session->session.selection_kind());
}

wirelab_str wirelab_selected_id(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.selected_id());
}

wirelab_str wirelab_selected_summary(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.selected_summary());
}

void wirelab_select_node(wirelab_session* session, const char* id)
{
  guard(session, [id](wirelab::Session& core) { core.select_node(arg_string(id)); });
}

void wirelab_select_link(wirelab_session* session, const char* from, const char* to)
{
  guard(session, [from, to](wirelab::Session& core) { core.select_link(arg_string(from), arg_string(to)); });
}

void wirelab_clear_selection(wirelab_session* session)
{
  guard(session, [](wirelab::Session& core) { core.clear_selection(); });
}

// ---- faults --------------------------------------------------------------

size_t wirelab_fault_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.active_faults().size();
}

wirelab_fault_view wirelab_fault_at(const wirelab_session* session, size_t index)
{
  wirelab_fault_view view{};
  if (session == nullptr)
    return view;
  const wirelab::FaultRow* row = row_at(session->session.active_faults(), index);
  if (row == nullptr)
    return view;
  view.first = to_str(row->first);
  view.second = to_str(row->second);
  view.latency_ms = row->latency_ms;
  view.loss_percent = row->loss_percent;
  view.blackhole = row->blackhole;
  // Derived rather than stored: the core distinguishes a link fault from a port
  // fault by whether the second endpoint is set, which C callers should not
  // have to rediscover.
  view.is_link = row->is_link();
  return view;
}

void wirelab_fault_apply_selected(wirelab_session* session, int32_t latency_ms, double loss_percent, bool blackhole)
{
  guard(
      session,
      [latency_ms, loss_percent, blackhole](wirelab::Session& core)
      { core.apply_selected_fault(latency_ms, loss_percent, blackhole); });
}

void wirelab_fault_clear(wirelab_session* session, const char* first_endpoint, const char* second_endpoint)
{
  guard(
      session,
      [first_endpoint, second_endpoint](wirelab::Session& core)
      { core.clear_fault(arg_string(first_endpoint), arg_string(second_endpoint)); });
}

// ---- policies ------------------------------------------------------------

size_t wirelab_policy_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.policy_rules().size();
}

wirelab_policy_view wirelab_policy_at(const wirelab_session* session, size_t index)
{
  wirelab_policy_view view{};
  if (session == nullptr)
    return view;
  const wirelab::PolicyRow* row = row_at(session->session.policy_rules(), index);
  if (row == nullptr)
    return view;
  view.name = to_str(row->name);
  view.anomaly_type = as_u32(row->anomaly_type);
  view.action = as_u32(row->action);
  view.enabled = row->enabled;
  view.rate_limit_packets_per_second = row->rate_limit_packets_per_second;
  view.hits = row->hits;
  return view;
}

size_t wirelab_policy_action_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.policy_actions().size();
}

wirelab_policy_action_view wirelab_policy_action_at(const wirelab_session* session, size_t index)
{
  wirelab_policy_action_view view{};
  if (session == nullptr)
    return view;
  const wirelab::PolicyActionRow* row = row_at(session->session.policy_actions(), index);
  if (row == nullptr)
    return view;
  view.sequence = row->sequence;
  view.rule = to_str(row->rule);
  view.anomaly_type = as_u32(row->anomaly_type);
  view.action = as_u32(row->action);
  view.port = to_str(row->port);
  view.outcome = as_u32(row->outcome);
  view.detail = to_str(row->detail);
  return view;
}

size_t wirelab_enforced_port_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.enforced_ports().size();
}

wirelab_enforced_port_view wirelab_enforced_port_at(const wirelab_session* session, size_t index)
{
  wirelab_enforced_port_view view{};
  if (session == nullptr)
    return view;
  const wirelab::EnforcedPortRow* row = row_at(session->session.enforced_ports(), index);
  if (row == nullptr)
    return view;
  view.port = to_str(row->port);
  view.rule = to_str(row->rule);
  view.kind = as_u32(row->kind);
  view.summary = to_str(row->summary);
  return view;
}

void wirelab_policy_add(
    wirelab_session* session,
    const char* name,
    const char* anomaly_type,
    const char* action,
    uint64_t rate_limit_packets_per_second)
{
  guard(
      session,
      [name, anomaly_type, action, rate_limit_packets_per_second](wirelab::Session& core) {
        core.add_policy(arg_string(name), arg_string(anomaly_type), arg_string(action), rate_limit_packets_per_second);
      });
}

void wirelab_policy_remove(wirelab_session* session, const char* name)
{
  guard(session, [name](wirelab::Session& core) { core.remove_policy(arg_string(name)); });
}

void wirelab_policy_set_enabled(wirelab_session* session, const char* name, bool enabled)
{
  guard(session, [name, enabled](wirelab::Session& core) { core.set_policy_enabled(arg_string(name), enabled); });
}

void wirelab_enforcement_release(wirelab_session* session, const char* port_id)
{
  guard(session, [port_id](wirelab::Session& core) { core.release_enforcement(arg_string(port_id)); });
}

// ---- traffic -------------------------------------------------------------

bool wirelab_traffic_running(const wirelab_session* session)
{
  return session != nullptr && session->session.traffic_running();
}

wirelab_str wirelab_active_backend(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.active_backend());
}

wirelab_str wirelab_traffic_result(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.traffic_result());
}

const wirelab_metric_sample* wirelab_metrics_history(const wirelab_session* session, size_t* out_count)
{
  if (session == nullptr)
  {
    if (out_count != nullptr)
      *out_count = 0U;
    return nullptr;
  }
  const std::vector<wirelab::MetricSample>& samples = session->session.metrics_history();
  if (out_count != nullptr)
    *out_count = samples.size();
  // Safe only because of the layout static_asserts above; this is the single
  // place the shim hands over core memory instead of copying field by field.
  return reinterpret_cast<const wirelab_metric_sample*>(samples.data());
}

size_t wirelab_mac_table_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.mac_table().size();
}

wirelab_mac_table_view wirelab_mac_table_at(const wirelab_session* session, size_t index)
{
  wirelab_mac_table_view view{};
  if (session == nullptr)
    return view;
  const wirelab::MacTableRow* row = row_at(session->session.mac_table(), index);
  if (row == nullptr)
    return view;
  view.mac = to_str(row->mac);
  view.port = to_str(row->port);
  return view;
}

size_t wirelab_port_state_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.port_states().size();
}

wirelab_port_state_view wirelab_port_state_at(const wirelab_session* session, size_t index)
{
  wirelab_port_state_view view{};
  if (session == nullptr)
    return view;
  const wirelab::PortStateRow* row = row_at(session->session.port_states(), index);
  if (row == nullptr)
    return view;
  view.id = to_str(row->id);
  view.enforced = row->enforced;
  view.received = row->received;
  view.forwarded = row->forwarded;
  view.dropped = row->dropped;
  return view;
}

size_t wirelab_packet_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.packet_rows().size();
}

wirelab_packet_view wirelab_packet_at(const wirelab_session* session, size_t index)
{
  wirelab_packet_view view{};
  if (session == nullptr)
    return view;
  const wirelab::PacketRow* row = row_at(session->session.packet_rows(), index);
  if (row == nullptr)
    return view;
  view.source_mac = to_str(row->source_mac);
  view.destination_mac = to_str(row->destination_mac);
  view.source_ip = to_str(row->source_ip);
  view.destination_ip = to_str(row->destination_ip);
  view.ingress = to_str(row->ingress);
  view.protocol = row->protocol;
  view.destination_port = row->destination_port;
  view.bytes = row->bytes;
  view.classification = as_u32(row->classification);
  view.validity = as_u32(row->validity);
  return view;
}

size_t wirelab_anomaly_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.anomaly_rows().size();
}

wirelab_anomaly_view wirelab_anomaly_at(const wirelab_session* session, size_t index)
{
  wirelab_anomaly_view view{};
  if (session == nullptr)
    return view;
  const wirelab::AnomalyRow* row = row_at(session->session.anomaly_rows(), index);
  if (row == nullptr)
    return view;
  view.type = as_u32(row->type);
  view.source_mac = to_str(row->source_mac);
  view.source_ip = to_str(row->source_ip);
  view.ingress_port = row->ingress_port;
  view.observed = row->observed;
  view.threshold = row->threshold;
  return view;
}

void wirelab_traffic_start(
    wirelab_session* session,
    const char* scenario,
    int32_t packets_per_tick,
    int32_t frame_size,
    uint64_t seed,
    const char* backend)
{
  guard(
      session,
      [scenario, packets_per_tick, frame_size, seed, backend](wirelab::Session& core) {
        core.start_traffic(arg_string(scenario), packets_per_tick, frame_size, seed, arg_string(backend));
      });
}

void wirelab_traffic_stop(wirelab_session* session)
{
  guard(session, [](wirelab::Session& core) { core.stop_traffic(); });
}

void wirelab_traffic_step(wirelab_session* session)
{
  guard(session, [](wirelab::Session& core) { core.run_traffic_step(); });
}

// ---- benchmark report ----------------------------------------------------

bool wirelab_report_running(const wirelab_session* session)
{
  return session != nullptr && session->session.report_running();
}

double wirelab_report_progress(const wirelab_session* session)
{
  return session == nullptr ? 0.0 : session->session.report_progress();
}

wirelab_str wirelab_report_stage(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.report_stage());
}

wirelab_str wirelab_report_export_path(const wirelab_session* session)
{
  return session == nullptr ? wirelab_str{} : to_str(session->session.report_export_path());
}

size_t wirelab_report_row_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.report_rows().size();
}

wirelab_report_row_view wirelab_report_row_at(const wirelab_session* session, size_t index)
{
  wirelab_report_row_view view{};
  if (session == nullptr)
    return view;
  const wirelab::ReportRow* row = row_at(session->session.report_rows(), index);
  if (row == nullptr)
    return view;
  view.backend_label = to_str(row->backend_label);
  view.backend_id = to_str(row->backend_id);
  view.scenario = to_str(row->scenario);
  view.packets = row->packets;
  view.elapsed_ns = row->elapsed_ns;
  view.packets_per_second = row->packets_per_second;
  view.goodput_bits_per_second = row->goodput_bits_per_second;
  view.loss_percent = row->loss_percent;
  view.latency_p50_ns = row->latency_p50_ns;
  view.latency_p95_ns = row->latency_p95_ns;
  view.latency_p99_ns = row->latency_p99_ns;
  view.host_to_device_ns = row->host_to_device_ns;
  view.kernel_ns = row->kernel_ns;
  view.device_to_host_ns = row->device_to_host_ns;
  view.transfer_inclusive_ns = row->transfer_inclusive_ns;
  view.queue_wait_ns = row->queue_wait_ns;
  view.speedup = row->speedup;
  return view;
}

wirelab_provenance_view wirelab_report_provenance(const wirelab_session* session)
{
  wirelab_provenance_view view{};
  if (session == nullptr)
    return view;
  const wirelab::ReportProvenance& provenance = session->session.report_provenance();
  view.scenario = to_str(provenance.scenario);
  view.seed = provenance.seed;
  view.packets = provenance.packets;
  view.batch_size = provenance.batch_size;
  view.frame_size = provenance.frame_size;
  view.host_count = provenance.host_count;
  view.generator = to_str(provenance.generator);
  view.version = to_str(provenance.version);
  view.build_type = to_str(provenance.build_type);
  view.generated_at = to_str(provenance.generated_at);
  // backends_compiled_in / backends_present are reached through the accessors
  // below so this stays a flat struct.
  return view;
}

size_t wirelab_report_compiled_in_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.report_provenance().backends_compiled_in.size();
}

wirelab_str wirelab_report_compiled_in_at(const wirelab_session* session, size_t index)
{
  if (session == nullptr)
    return wirelab_str{};
  const std::string* entry = row_at(session->session.report_provenance().backends_compiled_in, index);
  return entry == nullptr ? wirelab_str{} : to_str(*entry);
}

size_t wirelab_report_present_count(const wirelab_session* session)
{
  return session == nullptr ? 0U : session->session.report_provenance().backends_present.size();
}

wirelab_str wirelab_report_present_at(const wirelab_session* session, size_t index)
{
  if (session == nullptr)
    return wirelab_str{};
  const std::string* entry = row_at(session->session.report_provenance().backends_present, index);
  return entry == nullptr ? wirelab_str{} : to_str(*entry);
}

void wirelab_report_start(
    wirelab_session* session,
    const char* scenario,
    int32_t packets,
    int32_t batch_size,
    int32_t frame_size,
    int32_t seed)
{
  guard(
      session,
      [scenario, packets, batch_size, frame_size, seed](wirelab::Session& core)
      { core.run_benchmark_report(arg_string(scenario), packets, batch_size, frame_size, seed); });
}

void wirelab_report_step(wirelab_session* session)
{
  guard(session, [](wirelab::Session& core) { core.run_report_step(); });
}

bool wirelab_report_export(wirelab_session* session, const char* path)
{
  return guard_bool(session, [path](wirelab::Session& core) { return core.export_report(arg_string(path)); });
}

// ---- static tables -------------------------------------------------------

size_t wirelab_backend_count(void)
{
  return backend_table().size();
}

wirelab_str wirelab_backend_name(size_t index)
{
  const std::string* entry = row_at(backend_table(), index);
  return entry == nullptr ? wirelab_str{} : to_str(*entry);
}

size_t wirelab_scenario_count(void)
{
  return scenario_table().size();
}

wirelab_str wirelab_scenario_name(size_t index)
{
  const std::string* entry = row_at(scenario_table(), index);
  return entry == nullptr ? wirelab_str{} : to_str(*entry);
}

size_t wirelab_anomaly_type_count(void)
{
  return kAnomalyOrderCount;
}

wirelab_str wirelab_anomaly_type_name(size_t index)
{
  if (index >= kAnomalyOrderCount)
    return wirelab_str{};
  return to_str(wirelab::display_name(kAnomalyOrder[index]));
}

size_t wirelab_policy_action_name_count(void)
{
  return kPolicyActionOrderCount;
}

wirelab_str wirelab_policy_action_name(size_t index)
{
  if (index >= kPolicyActionOrderCount)
    return wirelab_str{};
  return to_str(wirelab::display_name(kPolicyActionOrder[index]));
}

wirelab_str wirelab_node_type_label(uint32_t node_type)
{
  switch (node_type)
  {
    case WIRELAB_NODE_HOST:
      return to_str(wirelab::to_string(wirelab::TopologyNodeType::Host));
    case WIRELAB_NODE_SWITCH:
      return to_str(wirelab::to_string(wirelab::TopologyNodeType::Switch));
    default:
      return wirelab_str{};
  }
}

wirelab_str wirelab_classification_label(uint32_t classification)
{
  switch (classification)
  {
    case WIRELAB_CLASSIFICATION_MALFORMED:
      return to_str(wirelab::display_name(wirelab::PacketClassification::Malformed));
    case WIRELAB_CLASSIFICATION_BROADCAST:
      return to_str(wirelab::display_name(wirelab::PacketClassification::Broadcast));
    case WIRELAB_CLASSIFICATION_UNKNOWN_UNICAST:
      return to_str(wirelab::display_name(wirelab::PacketClassification::UnknownUnicast));
    case WIRELAB_CLASSIFICATION_KNOWN_UNICAST:
      return to_str(wirelab::display_name(wirelab::PacketClassification::KnownUnicast));
    default:
      return wirelab_str{};
  }
}

wirelab_str wirelab_validity_label(uint32_t validity)
{
  switch (validity)
  {
    case WIRELAB_VALIDITY_VALID:
      return to_str(wirelab::display_name(wirelab::PacketValidity::Valid));
    case WIRELAB_VALIDITY_MALFORMED_ETHERNET:
      return to_str(wirelab::display_name(wirelab::PacketValidity::MalformedEthernet));
    case WIRELAB_VALIDITY_MALFORMED_IPV4:
      return to_str(wirelab::display_name(wirelab::PacketValidity::MalformedIpv4));
    case WIRELAB_VALIDITY_MALFORMED_TRANSPORT:
      return to_str(wirelab::display_name(wirelab::PacketValidity::MalformedTransport));
    default:
      return wirelab_str{};
  }
}

wirelab_str wirelab_anomaly_type_label(uint32_t anomaly_type)
{
  if (anomaly_type >= kAnomalyOrderCount)
    return wirelab_str{};
  // The cast is only sound because the enum static_asserts above pin every
  // AnomalyType enumerator to its C counterpart; kAnomalyOrderCount is the
  // same seven values, so it doubles as the range bound.
  return to_str(wirelab::display_name(static_cast<wirelab::AnomalyType>(anomaly_type)));
}

wirelab_str wirelab_policy_action_label(uint32_t action)
{
  if (action > WIRELAB_POLICY_ALERT_ONLY)
    return wirelab_str{};
  return to_str(wirelab::display_name(static_cast<wirelab::PolicyAction>(action)));
}

wirelab_str wirelab_enforcement_kind_label(uint32_t kind)
{
  if (kind > WIRELAB_ENFORCEMENT_ISOLATE)
    return wirelab_str{};
  return to_str(wirelab::to_string(static_cast<wirelab::EnforcementKind>(kind)));
}

wirelab_str wirelab_enforcement_outcome_label(uint32_t outcome)
{
  if (outcome > WIRELAB_OUTCOME_REJECTED)
    return wirelab_str{};
  return to_str(wirelab::to_string(static_cast<wirelab::EnforcementOutcome>(outcome)));
}

// ---- ABI self-check ------------------------------------------------------

void wirelab_abi_layout(wirelab_abi_layout_report* out)
{
  if (out == nullptr)
    return;
  out->abi_version = WIRELAB_FFI_ABI_VERSION;
  out->str = abi_type_of<wirelab_str>();
  out->node = abi_type_of<wirelab_node_view>();
  out->link = abi_type_of<wirelab_link_view>();
  out->metric_sample = abi_type_of<wirelab_metric_sample>();
  out->mac_table = abi_type_of<wirelab_mac_table_view>();
  out->port_state = abi_type_of<wirelab_port_state_view>();
  out->packet = abi_type_of<wirelab_packet_view>();
  out->anomaly = abi_type_of<wirelab_anomaly_view>();
  out->fault = abi_type_of<wirelab_fault_view>();
  out->policy = abi_type_of<wirelab_policy_view>();
  out->policy_action = abi_type_of<wirelab_policy_action_view>();
  out->enforced_port = abi_type_of<wirelab_enforced_port_view>();
  out->report_row = abi_type_of<wirelab_report_row_view>();
  out->provenance = abi_type_of<wirelab_provenance_view>();
}
