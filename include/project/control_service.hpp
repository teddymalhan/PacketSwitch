#ifndef PROJECT_CONTROL_SERVICE_HPP_
#define PROJECT_CONTROL_SERVICE_HPP_
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "project/control_protocol.hpp"
#include "project/topology_controller.hpp"
#include "project/vswitch.hpp"

namespace project
{
  struct ControlDispatch
  {
    ControlReply reply;
    std::optional<SwitchMetricsEvent> metrics_event;
    std::optional<FaultStateEvent> fault_event;
    std::optional<TopologyStateEvent> topology_event;
  };

  class ControlService
  {
   public:
    explicit ControlService(VSwitch& vswitch, uint64_t topology_revision = 0) noexcept;
    ControlService(VSwitch& vswitch, TopologyController& topology_controller) noexcept;

    [[nodiscard]] ControlDispatch dispatch(std::string_view json);
    [[nodiscard]] uint64_t topology_revision() const noexcept;

   private:
    [[nodiscard]] uint64_t current_topology_revision() const noexcept;
    [[nodiscard]] ControlReply reject(std::string request_id, std::string error) const;
    VSwitch& vswitch_;
    uint64_t topology_revision_;
    std::optional<std::reference_wrapper<TopologyController>> topology_controller_;
    uint64_t next_event_sequence_ = 1;
  };
}

#endif
