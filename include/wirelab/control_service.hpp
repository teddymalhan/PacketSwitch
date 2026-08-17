#ifndef PROJECT_CONTROL_SERVICE_HPP_
#define PROJECT_CONTROL_SERVICE_HPP_
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/control_protocol.hpp"
#include "wirelab/topology_controller.hpp"
#include "wirelab/vswitch.hpp"

namespace wirelab
{
  struct AnalysisEventDispatch
  {
    std::vector<AnomalyDetectedEvent> anomaly_events;
    std::vector<PolicyActionEvent> policy_events;
    // Enforced and released policy faults, reported over the existing fault
    // contract so a client needs no new event type to observe enforcement.
    std::vector<FaultStateEvent> fault_events;
    std::vector<EnforcementAction> enforcement_actions;
  };

  struct ControlDispatch
  {
    ControlReply reply;
    std::optional<SwitchMetricsEvent> metrics_event;
    std::vector<FaultStateEvent> fault_events;
    std::optional<TopologyStateEvent> topology_event;
    std::optional<SupervisionStateEvent> supervision_event;
    std::optional<BenchmarkProgressEvent> benchmark_progress_event;
    std::optional<BenchmarkResultEvent> benchmark_result_event;
  };

  // What one slice of an active benchmark produced. A slice that completes the
  // run reports both: the progress that took it to the end, and the result.
  struct BenchmarkEventDispatch
  {
    std::optional<BenchmarkProgressEvent> progress_event;
    std::optional<BenchmarkResultEvent> result_event;
  };

  class ControlService
  {
   public:
    explicit ControlService(VSwitch& vswitch, uint64_t topology_revision = 0) noexcept;
    ControlService(VSwitch& vswitch, TopologyController& topology_controller) noexcept;

    // How the service reaches live supervision state. The switch's supervisor
    // owns those counters and the layering runs supervisor -> server ->
    // service, so the service is handed a supplier rather than made to depend
    // on a SwitchSupervisor it sits underneath. Without one, the switch is
    // unsupervised and get_supervision_state is refused rather than answered
    // with zeroes.
    using SupervisionSource = std::function<SupervisionSnapshot()>;
    void set_supervision_source(SupervisionSource source);

    // How the service reaches analyzer backends it cannot link. The core
    // library knows only CPU; a host built with CUDA or Metal supplies a
    // factory that also answers for those, and a backend nobody can build is
    // refused rather than silently downgraded to CPU.
    void set_benchmark_backends(BenchmarkBackendFactory factory);

    [[nodiscard]] ControlDispatch dispatch(std::string_view json);
    // Stamps one pipeline outcome as revisioned control events. Fault events are
    // only emitted when the service was constructed with a topology controller,
    // because there is then a port whose configuration the client can be told
    // about. The pipeline is run by whoever owns it, so a switch and a capture
    // replay can share this framing without sharing a driver.
    [[nodiscard]] AnalysisEventDispatch analysis_events(AnalysisOutcome outcome);
    [[nodiscard]] SupervisionStateEvent supervision_event(SupervisionSnapshot snapshot);
    // Runs at most max_packets more of the active benchmark and reports what
    // moved. Called by whoever polls the control plane, so a benchmark advances
    // in slices between frames instead of blocking the switch loop until it
    // finishes.
    [[nodiscard]] BenchmarkEventDispatch advance_benchmark(size_t max_packets);
    [[nodiscard]] bool benchmark_active() const noexcept;
    [[nodiscard]] uint64_t topology_revision() const noexcept;

   private:
    [[nodiscard]] uint64_t current_topology_revision() const noexcept;
    [[nodiscard]] ControlReply accept(std::string request_id, std::string operation_id) const;
    // Rejections are whole dispatches, not bare replies: a refusal produces no
    // events, and saying so once here keeps every refusal site to one line.
    [[nodiscard]] ControlDispatch reject(std::string request_id, std::string error) const;
    [[nodiscard]] ControlDispatch start_benchmark(const ControlRequest& request);
    [[nodiscard]] ControlDispatch stop_run(const ControlRequest& request);
    [[nodiscard]] BenchmarkProgressEvent progress_event();
    [[nodiscard]] BenchmarkResultEvent result_event(bool completed);

    VSwitch& vswitch_;
    uint64_t topology_revision_;
    std::optional<std::reference_wrapper<TopologyController>> topology_controller_;
    SupervisionSource supervision_source_;
    BenchmarkBackendFactory benchmark_backends_;
    std::optional<BenchmarkRun> benchmark_run_;
    std::string benchmark_operation_id_;
    // A duration-limited run stops on whichever comes first, its packet budget
    // or this ceiling on measured benchmark time.
    uint64_t benchmark_deadline_ns_ = 0;
    uint64_t next_benchmark_id_ = 1;
    uint64_t next_event_sequence_ = 1;
  };
}  // namespace wirelab

#endif
