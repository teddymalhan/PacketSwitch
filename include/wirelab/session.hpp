#ifndef PROJECT_WIRELAB_SESSION_HPP_
#define PROJECT_WIRELAB_SESSION_HPP_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/anomaly_detector.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/policy_enforcer.hpp"
#include "wirelab/topology_controller.hpp"
#include "wirelab/traffic_generator.hpp"

namespace wirelab
{
  // Session is the Qt-free orchestration layer a desktop frontend drives: it
  // owns the lab state (topology, faults, policies, traffic, benchmark report)
  // and republishes it as plain rows. It has no clock of its own -- the caller
  // steps it via run_traffic_step() / run_report_step() -- and it is not
  // thread-safe; one session belongs to one thread.
  //
  // Where the old Qt view model emitted a signal, the session sets a bit in a
  // dirty mask the caller drains with take_dirty(). That is the whole change of
  // shape: everything else about the workflow is preserved exactly.

  enum class SessionDirty : uint32_t
  {
    None = 0U,
    Topology = 1U << 0U,
    Selection = 1U << 1U,
    Status = 1U << 2U,
    TrafficState = 1U << 3U,
    Telemetry = 1U << 4U,
    Faults = 1U << 5U,
    Policies = 1U << 6U,
    Report = 1U << 7U,
  };

  [[nodiscard]] constexpr uint32_t dirty_bit(SessionDirty flag) noexcept
  {
    return static_cast<uint32_t>(flag);
  }

  // Human-facing spellings. These are deliberately not the wire spellings
  // to_string() produces: the wire names are stable identifiers, these are
  // labels a person reads. Both directions live here so the frontend and the
  // FFI agree on one vocabulary.
  [[nodiscard]] const char* display_name(AnomalyType type) noexcept;
  [[nodiscard]] const char* display_name(PolicyAction action) noexcept;
  [[nodiscard]] const char* display_name(PacketClassification classification) noexcept;
  [[nodiscard]] const char* display_name(PacketValidity validity) noexcept;
  [[nodiscard]] bool anomaly_type_from_display_name(const std::string& name, AnomalyType& type) noexcept;
  [[nodiscard]] bool policy_action_from_display_name(const std::string& name, PolicyAction& action) noexcept;
  // Scenario names are the wire names the benchmark engine parses; the traffic
  // form and the report form offer exactly these.
  [[nodiscard]] const std::vector<std::string>& scenario_names();

  struct NodeRow
  {
    std::string id;
    TopologyNodeType type = TopologyNodeType::Host;
    // Normalised layout coordinates in [0, 1]; switches sit at the centre and
    // hosts are placed on a circle around them.
    double x = 0.5;
    double y = 0.5;
  };

  struct LinkRow
  {
    std::string from;
    std::string to;
    int64_t latency_ms = 0;
  };

  struct MetricSample
  {
    uint64_t sequence = 0;
    double throughput_mbps = 0.0;
    double latency_ms = 0.0;
    double loss_percent = 0.0;
  };

  struct MacTableRow
  {
    std::string mac;
    std::string port;
  };

  struct PortStateRow
  {
    std::string id;
    bool enforced = false;
    uint64_t received = 0;
    uint64_t forwarded = 0;
    uint64_t dropped = 0;
  };

  struct PacketRow
  {
    std::string source_mac;
    std::string destination_mac;
    std::string source_ip;
    std::string destination_ip;
    std::string ingress;
    uint8_t protocol = 0;
    uint16_t destination_port = 0;
    uint16_t bytes = 0;
    PacketClassification classification = PacketClassification::Malformed;
    PacketValidity validity = PacketValidity::MalformedEthernet;
  };

  struct AnomalyRow
  {
    AnomalyType type = AnomalyType::BroadcastStorm;
    std::string source_mac;
    std::string source_ip;
    uint32_t ingress_port = 0;
    uint64_t observed = 0;
    uint64_t threshold = 0;
  };

  struct FaultRow
  {
    std::string first;
    // Empty for a port fault; both endpoints set for a link fault.
    std::string second;
    int64_t latency_ms = 0;
    double loss_percent = 0.0;
    bool blackhole = false;

    [[nodiscard]] bool is_link() const noexcept
    {
      return !second.empty();
    }
  };

  struct PolicyRow
  {
    std::string name;
    AnomalyType anomaly_type = AnomalyType::BroadcastStorm;
    PolicyAction action = PolicyAction::AlertOnly;
    bool enabled = true;
    uint64_t rate_limit_packets_per_second = 0;
    uint64_t hits = 0;
  };

  struct PolicyActionRow
  {
    uint64_t sequence = 0;
    std::string rule;
    AnomalyType anomaly_type = AnomalyType::BroadcastStorm;
    PolicyAction action = PolicyAction::AlertOnly;
    std::string port;
    EnforcementOutcome outcome = EnforcementOutcome::Skipped;
    std::string detail;
  };

  struct EnforcedPortRow
  {
    std::string port;
    std::string rule;
    EnforcementKind kind = EnforcementKind::None;
    std::string summary;
  };

  struct ReportRow
  {
    std::string backend_label;
    std::string backend_id;
    std::string scenario;
    uint64_t packets = 0;
    uint64_t elapsed_ns = 0;
    double packets_per_second = 0.0;
    double goodput_bits_per_second = 0.0;
    double loss_percent = 0.0;
    uint64_t latency_p50_ns = 0;
    uint64_t latency_p95_ns = 0;
    uint64_t latency_p99_ns = 0;
    uint64_t host_to_device_ns = 0;
    uint64_t kernel_ns = 0;
    uint64_t device_to_host_ns = 0;
    uint64_t transfer_inclusive_ns = 0;
    uint64_t queue_wait_ns = 0;
    double speedup = 0.0;
  };

  struct ReportProvenance
  {
    std::string scenario;
    uint64_t seed = 0;
    uint64_t packets = 0;
    uint64_t batch_size = 0;
    uint64_t frame_size = 0;
    uint64_t host_count = 0;
    std::string generator;
    std::string version;
    std::string build_type;
    std::vector<std::string> backends_compiled_in;
    std::vector<std::string> backends_present;
    // ISO-8601 UTC, second resolution, e.g. 2026-08-17T12:34:56Z.
    std::string generated_at;
  };

  enum class SelectionKind
  {
    None,
    Node,
    Link
  };

  class Session final
  {
   public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    // Returns the accumulated change bits and clears them.
    [[nodiscard]] uint32_t take_dirty() noexcept;

    [[nodiscard]] const std::string& status_message() const noexcept;

    // ---- topology -------------------------------------------------------
    [[nodiscard]] bool has_topology() const noexcept;
    [[nodiscard]] const std::string& topology_name() const noexcept;
    [[nodiscard]] const std::vector<NodeRow>& topology_nodes() const noexcept;
    [[nodiscard]] const std::vector<LinkRow>& topology_links() const noexcept;
    // Accepts a plain path or a file:// URL, matching what a native file dialog
    // hands back.
    void open_topology(const std::string& path);
    void save_topology(const std::string& path);
    // type is "switch" or "host", matched case-insensitively.
    void add_node(const std::string& id, const std::string& type);
    void add_link(const std::string& from, const std::string& to, int32_t latency_ms);
    void remove_selected();

    // ---- selection ------------------------------------------------------
    [[nodiscard]] SelectionKind selection_kind() const noexcept;
    [[nodiscard]] const std::string& selected_id() const noexcept;
    [[nodiscard]] const std::string& selected_summary() const noexcept;
    void select_node(const std::string& id);
    void select_link(const std::string& from, const std::string& to);
    void clear_selection();

    // ---- faults ---------------------------------------------------------
    [[nodiscard]] const std::vector<FaultRow>& active_faults() const noexcept;
    void apply_selected_fault(int32_t latency_ms, double loss_percent, bool blackhole);
    // An empty second endpoint clears a port fault, otherwise a link fault.
    void clear_fault(const std::string& first_endpoint, const std::string& second_endpoint);

    // ---- policies -------------------------------------------------------
    [[nodiscard]] const std::vector<PolicyRow>& policy_rules() const noexcept;
    [[nodiscard]] const std::vector<PolicyActionRow>& policy_actions() const noexcept;
    [[nodiscard]] const std::vector<EnforcedPortRow>& enforced_ports() const noexcept;
    // anomaly_type and action are display names; an unknown one is reported
    // through status_message() and changes nothing.
    void add_policy(const std::string& name, const std::string& anomaly_type, const std::string& action, uint64_t rate_limit_pps);
    void remove_policy(const std::string& name);
    void set_policy_enabled(const std::string& name, bool enabled);
    void release_enforcement(const std::string& port_id);

    // ---- traffic --------------------------------------------------------
    // Backends this build was compiled with and this machine actually has,
    // labelled the way a person reads them ("CPU", "CUDA", "Metal").
    [[nodiscard]] static const std::vector<std::string>& available_backends();
    [[nodiscard]] bool traffic_running() const noexcept;
    [[nodiscard]] const std::string& active_backend() const noexcept;
    [[nodiscard]] const std::string& traffic_result() const noexcept;
    [[nodiscard]] const std::vector<MetricSample>& metrics_history() const noexcept;
    [[nodiscard]] const std::vector<MacTableRow>& mac_table() const noexcept;
    [[nodiscard]] const std::vector<PortStateRow>& port_states() const noexcept;
    [[nodiscard]] const std::vector<PacketRow>& packet_rows() const noexcept;
    [[nodiscard]] const std::vector<AnomalyRow>& anomaly_rows() const noexcept;
    // scenario is a wire name from scenario_names(); an unknown scenario, a
    // non-positive tick size, a frame smaller than an Ethernet header or an
    // unavailable backend is reported through status_message() and leaves
    // traffic stopped.
    void start_traffic(
        const std::string& scenario,
        int32_t packets_per_tick,
        int32_t frame_size,
        uint64_t seed,
        const std::string& backend);
    void stop_traffic();
    // One simulation tick: generate, forward through the fault model, analyse,
    // evaluate policies, republish telemetry. A no-op when traffic is stopped.
    void run_traffic_step();

    // ---- benchmark report -----------------------------------------------
    [[nodiscard]] bool report_running() const noexcept;
    [[nodiscard]] double report_progress() const noexcept;
    [[nodiscard]] const std::string& report_stage() const noexcept;
    [[nodiscard]] const std::vector<ReportRow>& report_rows() const noexcept;
    [[nodiscard]] const ReportProvenance& report_provenance() const noexcept;
    [[nodiscard]] const std::string& report_export_path() const noexcept;
    void run_benchmark_report(
        const std::string& scenario,
        int32_t packets,
        int32_t batch_size,
        int32_t frame_size,
        int32_t seed);
    // Advances the running report by one slice; a no-op when none is running.
    void run_report_step();
    // Writes <path>.json and <path>.csv; a .json suffix on the path is
    // tolerated and stripped. Returns false and reports why via
    // report_export_path()/status_message() on failure.
    bool export_report(const std::string& path);

   private:
    struct PortCounters
    {
      uint64_t received = 0;
      uint64_t forwarded = 0;
      uint64_t dropped = 0;
    };

    void mark(SessionDirty flag) noexcept;
    void set_status(std::string message);
    bool commit_topology(TopologyConfiguration configuration, const std::string& success_message);
    void rebuild_topology_rows();
    void rebuild_fault_rows();
    void rebuild_policy_rows();
    void rebuild_telemetry_rows(
        const AnalysisBatch& analysis,
        uint64_t tick_dropped,
        double throughput_mbps,
        double average_latency_ms);
    void reset_simulation();
    // Arms the next queued backend; answers whether there was one left to arm.
    bool begin_next_report_backend();
    void finish_report();
    void rebuild_report_rows();
    void rebuild_report_provenance();

    uint32_t dirty_ = 0;
    std::string status_message_;

    TopologyController topology_controller_;
    TopologyConfiguration topology_configuration_;
    std::string topology_path_;
    std::vector<NodeRow> topology_nodes_;
    std::vector<LinkRow> topology_links_;

    SelectionKind selection_kind_ = SelectionKind::None;
    std::string selected_id_;
    std::string selected_first_;
    std::string selected_second_;
    std::string selected_summary_;

    bool traffic_running_ = false;
    std::string active_backend_ = "CPU";
    TrafficScenario traffic_scenario_ = TrafficScenario::Mixed;
    int32_t packets_per_tick_ = 256;
    int32_t frame_size_ = 64;
    uint64_t traffic_seed_ = 1;
    uint64_t tick_sequence_ = 0;
    uint64_t total_packets_ = 0;
    uint64_t total_bytes_ = 0;
    uint64_t total_dropped_ = 0;
    std::unique_ptr<DeterministicTrafficGenerator> traffic_generator_;
    std::unique_ptr<PacketAnalyzer> traffic_analyzer_;
    AnalysisPipeline analysis_pipeline_;
    std::unordered_map<std::string, PortCounters> port_counters_;
    std::chrono::steady_clock::time_point simulation_start_{};
    std::unordered_map<std::string, std::string> learned_mac_ports_;

    std::string traffic_result_;
    std::vector<MetricSample> metrics_history_;
    std::vector<MacTableRow> mac_table_;
    std::vector<PortStateRow> port_states_;
    std::vector<PacketRow> packet_rows_;
    std::vector<AnomalyRow> anomaly_rows_;
    std::vector<FaultRow> active_faults_;
    std::vector<PolicyRow> policy_rules_;
    std::vector<PolicyActionRow> policy_actions_;
    std::vector<EnforcedPortRow> enforced_ports_;

    bool report_running_ = false;
    double report_progress_ = 0.0;
    std::string report_stage_;
    std::string report_export_path_;
    BenchmarkConfig report_config_;
    std::vector<std::string> report_queue_;
    size_t report_index_ = 0;
    size_t report_slice_budget_ = 0;
    uint64_t report_completed_packets_ = 0;
    uint64_t report_total_packets_ = 0;
    std::optional<BenchmarkRun> report_run_;
    std::vector<BenchmarkResult> report_results_;
    std::vector<ReportRow> report_rows_;
    ReportProvenance report_provenance_;
  };
}  // namespace wirelab

#endif
