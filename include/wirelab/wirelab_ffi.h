#ifndef PROJECT_WIRELAB_FFI_H_
#define PROJECT_WIRELAB_FFI_H_

/*
 * The C ABI a native frontend drives WireLab through.
 *
 * Shape rules, all of them load-bearing:
 *
 *  - A session is NOT thread-safe. One session belongs to one thread; the core
 *    it wraps is single-threaded by design.
 *  - Every wirelab_str and every string inside a returned view is BORROWED from
 *    session-owned storage. It stays valid until the next call that mutates the
 *    session (anything that is not a pure getter). Copy it if you need to keep
 *    it. Static tables (wirelab_backend_name and friends) are valid forever.
 *  - Commands do not return status codes. The session's contract is that a
 *    rejected command reports why through wirelab_session_status_message() and
 *    raises WIRELAB_DIRTY_STATUS; a second, invented error channel could only
 *    disagree with it. The two exceptions are wirelab_session_open (NULL on ABI
 *    mismatch) and wirelab_report_export (false when nothing was written).
 *  - The shim never lets a C++ exception cross. One that escapes is turned into
 *    a status message plus WIRELAB_DIRTY_STATUS.
 *  - Enum values here mirror the C++ enums exactly; the shim static_asserts it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Bumped whenever any signature, struct layout or enum value below changes. */
#define WIRELAB_FFI_ABI_VERSION 1u

  typedef struct wirelab_session wirelab_session;

  /* Borrowed UTF-8. Not NUL-terminated; always use len. */
  typedef struct
  {
    const char* ptr;
    size_t len;
  } wirelab_str;

  /* One bit per category of change. Mirrors wirelab::SessionDirty. */
  enum
  {
    WIRELAB_DIRTY_TOPOLOGY = 1u << 0u,
    WIRELAB_DIRTY_SELECTION = 1u << 1u,
    WIRELAB_DIRTY_STATUS = 1u << 2u,
    WIRELAB_DIRTY_TRAFFIC_STATE = 1u << 3u,
    WIRELAB_DIRTY_TELEMETRY = 1u << 4u,
    WIRELAB_DIRTY_FAULTS = 1u << 5u,
    WIRELAB_DIRTY_POLICIES = 1u << 6u,
    WIRELAB_DIRTY_REPORT = 1u << 7u
  };

  typedef enum
  {
    WIRELAB_NODE_HOST = 0,
    WIRELAB_NODE_SWITCH = 1
  } wirelab_node_type;

  typedef enum
  {
    WIRELAB_SELECTION_NONE = 0,
    WIRELAB_SELECTION_NODE = 1,
    WIRELAB_SELECTION_LINK = 2
  } wirelab_selection_kind;

  typedef enum
  {
    WIRELAB_CLASSIFICATION_MALFORMED = 0,
    WIRELAB_CLASSIFICATION_BROADCAST = 1,
    WIRELAB_CLASSIFICATION_UNKNOWN_UNICAST = 2,
    WIRELAB_CLASSIFICATION_KNOWN_UNICAST = 3
  } wirelab_classification;

  typedef enum
  {
    WIRELAB_VALIDITY_VALID = 0,
    WIRELAB_VALIDITY_MALFORMED_ETHERNET = 1,
    WIRELAB_VALIDITY_MALFORMED_IPV4 = 2,
    WIRELAB_VALIDITY_MALFORMED_TRANSPORT = 3
  } wirelab_validity;

  typedef enum
  {
    WIRELAB_ANOMALY_BROADCAST_STORM = 0,
    WIRELAB_ANOMALY_MAC_FLAP = 1,
    WIRELAB_ANOMALY_UNKNOWN_UNICAST_FLOOD = 2,
    WIRELAB_ANOMALY_UDP_FLOOD = 3,
    WIRELAB_ANOMALY_PORT_SCAN = 4,
    WIRELAB_ANOMALY_HOT_TALKER = 5,
    WIRELAB_ANOMALY_MALFORMED_FRAME = 6
  } wirelab_anomaly_type;

  typedef enum
  {
    WIRELAB_POLICY_ALLOW = 0,
    WIRELAB_POLICY_DROP = 1,
    WIRELAB_POLICY_MIRROR = 2,
    WIRELAB_POLICY_RATE_LIMIT = 3,
    WIRELAB_POLICY_QUARANTINE = 4,
    WIRELAB_POLICY_ALERT_ONLY = 5
  } wirelab_policy_action;

  typedef enum
  {
    WIRELAB_ENFORCEMENT_NONE = 0,
    WIRELAB_ENFORCEMENT_RATE_LIMIT = 1,
    WIRELAB_ENFORCEMENT_BLACKHOLE = 2,
    WIRELAB_ENFORCEMENT_ISOLATE = 3
  } wirelab_enforcement_kind;

  typedef enum
  {
    WIRELAB_OUTCOME_APPLIED = 0,
    WIRELAB_OUTCOME_EXTENDED = 1,
    WIRELAB_OUTCOME_RELEASED = 2,
    WIRELAB_OUTCOME_SKIPPED = 3,
    WIRELAB_OUTCOME_UNKNOWN_PORT = 4,
    WIRELAB_OUTCOME_REJECTED = 5
  } wirelab_enforcement_outcome;

  /* ---- row views ------------------------------------------------------- */

  typedef struct
  {
    wirelab_str id;
    uint32_t type; /* wirelab_node_type */
    double x;      /* normalised layout coordinates in [0, 1] */
    double y;
  } wirelab_node_view;

  typedef struct
  {
    wirelab_str from;
    wirelab_str to;
    int64_t latency_ms;
  } wirelab_link_view;

  /* The one genuinely POD row: handed over as a contiguous span, not per-row. */
  typedef struct
  {
    uint64_t sequence;
    double throughput_mbps;
    double latency_ms;
    double loss_percent;
  } wirelab_metric_sample;

  typedef struct
  {
    wirelab_str mac;
    wirelab_str port;
  } wirelab_mac_table_view;

  typedef struct
  {
    wirelab_str id;
    bool enforced;
    uint64_t received;
    uint64_t forwarded;
    uint64_t dropped;
  } wirelab_port_state_view;

  typedef struct
  {
    wirelab_str source_mac;
    wirelab_str destination_mac;
    wirelab_str source_ip;
    wirelab_str destination_ip;
    wirelab_str ingress;
    uint8_t protocol;
    uint16_t destination_port;
    uint16_t bytes;
    uint32_t classification; /* wirelab_classification */
    uint32_t validity;       /* wirelab_validity */
  } wirelab_packet_view;

  typedef struct
  {
    uint32_t type; /* wirelab_anomaly_type */
    wirelab_str source_mac;
    wirelab_str source_ip;
    uint32_t ingress_port;
    uint64_t observed;
    uint64_t threshold;
  } wirelab_anomaly_view;

  typedef struct
  {
    wirelab_str first;
    wirelab_str second; /* empty for a port fault */
    int64_t latency_ms;
    double loss_percent;
    bool blackhole;
    bool is_link;
  } wirelab_fault_view;

  typedef struct
  {
    wirelab_str name;
    uint32_t anomaly_type; /* wirelab_anomaly_type */
    uint32_t action;       /* wirelab_policy_action */
    bool enabled;
    uint64_t rate_limit_packets_per_second;
    uint64_t hits;
  } wirelab_policy_view;

  typedef struct
  {
    uint64_t sequence;
    wirelab_str rule;
    uint32_t anomaly_type; /* wirelab_anomaly_type */
    uint32_t action;       /* wirelab_policy_action */
    wirelab_str port;
    uint32_t outcome; /* wirelab_enforcement_outcome */
    wirelab_str detail;
  } wirelab_policy_action_view;

  typedef struct
  {
    wirelab_str port;
    wirelab_str rule;
    uint32_t kind; /* wirelab_enforcement_kind */
    wirelab_str summary;
  } wirelab_enforced_port_view;

  typedef struct
  {
    wirelab_str backend_label;
    wirelab_str backend_id;
    wirelab_str scenario;
    uint64_t packets;
    uint64_t elapsed_ns;
    double packets_per_second;
    double goodput_bits_per_second;
    double loss_percent;
    uint64_t latency_p50_ns;
    uint64_t latency_p95_ns;
    uint64_t latency_p99_ns;
    uint64_t host_to_device_ns;
    uint64_t kernel_ns;
    uint64_t device_to_host_ns;
    uint64_t transfer_inclusive_ns;
    uint64_t queue_wait_ns;
    double speedup;
  } wirelab_report_row_view;

  /* The two backend name lists are reached through the accessors below rather
   * than embedded, so this stays a flat struct. */
  typedef struct
  {
    wirelab_str scenario;
    uint64_t seed;
    uint64_t packets;
    uint64_t batch_size;
    uint64_t frame_size;
    uint64_t host_count;
    wirelab_str generator;
    wirelab_str version;
    wirelab_str build_type;
    wirelab_str generated_at;
  } wirelab_provenance_view;

  /* ---- lifecycle ------------------------------------------------------- */

  uint32_t wirelab_ffi_abi_version(void);
  /* NULL when abi_version != WIRELAB_FFI_ABI_VERSION or allocation fails. */
  wirelab_session* wirelab_session_open(uint32_t abi_version);
  void wirelab_session_close(wirelab_session* session);
  /* Returns the accumulated WIRELAB_DIRTY_* bits and clears them. */
  uint32_t wirelab_session_take_dirty(wirelab_session* session);
  wirelab_str wirelab_session_status_message(const wirelab_session* session);

  /* ---- topology -------------------------------------------------------- */

  bool wirelab_topology_loaded(const wirelab_session* session);
  wirelab_str wirelab_topology_name(const wirelab_session* session);
  size_t wirelab_topology_node_count(const wirelab_session* session);
  wirelab_node_view wirelab_topology_node_at(const wirelab_session* session, size_t index);
  size_t wirelab_topology_link_count(const wirelab_session* session);
  wirelab_link_view wirelab_topology_link_at(const wirelab_session* session, size_t index);
  /* path accepts a plain path or a file:// URL. */
  void wirelab_topology_open(wirelab_session* session, const char* path);
  void wirelab_topology_save(wirelab_session* session, const char* path);
  /* type is "switch" or "host", matched case-insensitively. */
  void wirelab_topology_add_node(wirelab_session* session, const char* id, const char* type);
  void wirelab_topology_add_link(wirelab_session* session, const char* from, const char* to, int32_t latency_ms);
  void wirelab_topology_remove_selected(wirelab_session* session);

  /* ---- selection ------------------------------------------------------- */

  uint32_t wirelab_selection_kind_of(const wirelab_session* session); /* wirelab_selection_kind */
  wirelab_str wirelab_selected_id(const wirelab_session* session);
  wirelab_str wirelab_selected_summary(const wirelab_session* session);
  void wirelab_select_node(wirelab_session* session, const char* id);
  void wirelab_select_link(wirelab_session* session, const char* from, const char* to);
  void wirelab_clear_selection(wirelab_session* session);

  /* ---- faults ---------------------------------------------------------- */

  size_t wirelab_fault_count(const wirelab_session* session);
  wirelab_fault_view wirelab_fault_at(const wirelab_session* session, size_t index);
  void wirelab_fault_apply_selected(wirelab_session* session, int32_t latency_ms, double loss_percent, bool blackhole);
  /* An empty or NULL second endpoint clears a port fault. */
  void wirelab_fault_clear(wirelab_session* session, const char* first_endpoint, const char* second_endpoint);

  /* ---- policies -------------------------------------------------------- */

  size_t wirelab_policy_count(const wirelab_session* session);
  wirelab_policy_view wirelab_policy_at(const wirelab_session* session, size_t index);
  size_t wirelab_policy_action_count(const wirelab_session* session);
  wirelab_policy_action_view wirelab_policy_action_at(const wirelab_session* session, size_t index);
  size_t wirelab_enforced_port_count(const wirelab_session* session);
  wirelab_enforced_port_view wirelab_enforced_port_at(const wirelab_session* session, size_t index);
  /* anomaly_type and action are display names from the static tables below. */
  void wirelab_policy_add(
      wirelab_session* session,
      const char* name,
      const char* anomaly_type,
      const char* action,
      uint64_t rate_limit_packets_per_second);
  void wirelab_policy_remove(wirelab_session* session, const char* name);
  void wirelab_policy_set_enabled(wirelab_session* session, const char* name, bool enabled);
  void wirelab_enforcement_release(wirelab_session* session, const char* port_id);

  /* ---- traffic --------------------------------------------------------- */

  bool wirelab_traffic_running(const wirelab_session* session);
  wirelab_str wirelab_active_backend(const wirelab_session* session);
  wirelab_str wirelab_traffic_result(const wirelab_session* session);
  /* Borrowed span of at most 60 samples, oldest first. */
  const wirelab_metric_sample* wirelab_metrics_history(const wirelab_session* session, size_t* out_count);
  size_t wirelab_mac_table_count(const wirelab_session* session);
  wirelab_mac_table_view wirelab_mac_table_at(const wirelab_session* session, size_t index);
  size_t wirelab_port_state_count(const wirelab_session* session);
  wirelab_port_state_view wirelab_port_state_at(const wirelab_session* session, size_t index);
  size_t wirelab_packet_count(const wirelab_session* session);
  wirelab_packet_view wirelab_packet_at(const wirelab_session* session, size_t index);
  size_t wirelab_anomaly_count(const wirelab_session* session);
  wirelab_anomaly_view wirelab_anomaly_at(const wirelab_session* session, size_t index);
  /* scenario is a wire name from wirelab_scenario_name(). */
  void wirelab_traffic_start(
      wirelab_session* session,
      const char* scenario,
      int32_t packets_per_tick,
      int32_t frame_size,
      uint64_t seed,
      const char* backend);
  void wirelab_traffic_stop(wirelab_session* session);
  void wirelab_traffic_step(wirelab_session* session);

  /* ---- benchmark report ------------------------------------------------ */

  bool wirelab_report_running(const wirelab_session* session);
  double wirelab_report_progress(const wirelab_session* session);
  wirelab_str wirelab_report_stage(const wirelab_session* session);
  wirelab_str wirelab_report_export_path(const wirelab_session* session);
  size_t wirelab_report_row_count(const wirelab_session* session);
  wirelab_report_row_view wirelab_report_row_at(const wirelab_session* session, size_t index);
  wirelab_provenance_view wirelab_report_provenance(const wirelab_session* session);
  size_t wirelab_report_compiled_in_count(const wirelab_session* session);
  wirelab_str wirelab_report_compiled_in_at(const wirelab_session* session, size_t index);
  size_t wirelab_report_present_count(const wirelab_session* session);
  wirelab_str wirelab_report_present_at(const wirelab_session* session, size_t index);
  void wirelab_report_start(
      wirelab_session* session,
      const char* scenario,
      int32_t packets,
      int32_t batch_size,
      int32_t frame_size,
      int32_t seed);
  void wirelab_report_step(wirelab_session* session);
  /* Writes <path>.json and <path>.csv; a .json suffix on path is stripped. */
  bool wirelab_report_export(wirelab_session* session, const char* path);

  /* ---- static tables --------------------------------------------------- */
  /* Process-lifetime storage; safe to hold indefinitely. An out-of-range index
   * yields an empty string rather than a trap. */

  size_t wirelab_backend_count(void);
  wirelab_str wirelab_backend_name(size_t index);
  size_t wirelab_scenario_count(void);
  wirelab_str wirelab_scenario_name(size_t index);
  size_t wirelab_anomaly_type_count(void);
  wirelab_str wirelab_anomaly_type_name(size_t index);
  size_t wirelab_policy_action_name_count(void);
  wirelab_str wirelab_policy_action_name(size_t index);

  wirelab_str wirelab_node_type_label(uint32_t node_type);
  wirelab_str wirelab_classification_label(uint32_t classification);
  wirelab_str wirelab_validity_label(uint32_t validity);
  wirelab_str wirelab_anomaly_type_label(uint32_t anomaly_type);
  wirelab_str wirelab_policy_action_label(uint32_t action);
  wirelab_str wirelab_enforcement_kind_label(uint32_t kind);
  wirelab_str wirelab_enforcement_outcome_label(uint32_t outcome);

  /* ---- ABI self-check -------------------------------------------------- */
  /* The binding side compares these against its own mirrors. A mismatch means
   * the two compilers disagree about layout, which no amount of careful
   * hand-mirroring can be trusted to catch by eye. */

  typedef struct
  {
    uint32_t size;
    uint32_t align;
  } wirelab_abi_type;

  typedef struct
  {
    uint32_t abi_version;
    wirelab_abi_type str;
    wirelab_abi_type node;
    wirelab_abi_type link;
    wirelab_abi_type metric_sample;
    wirelab_abi_type mac_table;
    wirelab_abi_type port_state;
    wirelab_abi_type packet;
    wirelab_abi_type anomaly;
    wirelab_abi_type fault;
    wirelab_abi_type policy;
    wirelab_abi_type policy_action;
    wirelab_abi_type enforced_port;
    wirelab_abi_type report_row;
    wirelab_abi_type provenance;
  } wirelab_abi_layout_report;

  void wirelab_abi_layout(wirelab_abi_layout_report* out);

#ifdef __cplusplus
}
#endif

#endif
