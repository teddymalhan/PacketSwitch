#ifndef PROJECT_CONTROL_SERVICE_HPP_
#define PROJECT_CONTROL_SERVICE_HPP_
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
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
  };

  class ControlService
  {
   public:
    explicit ControlService(VSwitch& vswitch, uint64_t topology_revision = 0) noexcept;
    ControlService(VSwitch& vswitch, TopologyController& topology_controller) noexcept;

    [[nodiscard]] ControlDispatch dispatch(std::string_view json);
    // Runs one analysed batch through the shared pipeline and republishes the
    // result as revisioned control events. Fault events are only emitted when
    // the service was constructed with a topology controller, because there is
    // then a port whose configuration the client can be told about.
    [[nodiscard]] AnalysisEventDispatch evaluate_analysis(
        const AnalysisBatch& batch,
        uint64_t timestamp_ns,
        AnalysisPipeline& pipeline,
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] uint64_t topology_revision() const noexcept;

   private:
    [[nodiscard]] uint64_t current_topology_revision() const noexcept;
    [[nodiscard]] ControlReply reject(std::string request_id, std::string error) const;
    VSwitch& vswitch_;
    uint64_t topology_revision_;
    std::optional<std::reference_wrapper<TopologyController>> topology_controller_;
    uint64_t next_event_sequence_ = 1;
  };
}  // namespace wirelab

#endif
