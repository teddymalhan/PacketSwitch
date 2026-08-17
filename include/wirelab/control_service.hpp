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
    std::optional<SupervisionStateEvent> supervision_event;
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

    [[nodiscard]] ControlDispatch dispatch(std::string_view json);
    // Stamps one pipeline outcome as revisioned control events. Fault events are
    // only emitted when the service was constructed with a topology controller,
    // because there is then a port whose configuration the client can be told
    // about. The pipeline is run by whoever owns it, so a switch and a capture
    // replay can share this framing without sharing a driver.
    [[nodiscard]] AnalysisEventDispatch analysis_events(AnalysisOutcome outcome);
    [[nodiscard]] SupervisionStateEvent supervision_event(SupervisionSnapshot snapshot);
    [[nodiscard]] uint64_t topology_revision() const noexcept;

   private:
    [[nodiscard]] uint64_t current_topology_revision() const noexcept;
    [[nodiscard]] ControlReply accept(std::string request_id, std::string operation_id) const;
    // Rejections are whole dispatches, not bare replies: a refusal produces no
    // events, and saying so once here keeps every refusal site to one line.
    [[nodiscard]] ControlDispatch reject(std::string request_id, std::string error) const;
    VSwitch& vswitch_;
    uint64_t topology_revision_;
    std::optional<std::reference_wrapper<TopologyController>> topology_controller_;
    SupervisionSource supervision_source_;
    uint64_t next_event_sequence_ = 1;
  };
}  // namespace wirelab

#endif
