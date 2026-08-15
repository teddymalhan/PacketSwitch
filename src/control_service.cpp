#include "project/control_service.hpp"

#include <utility>

namespace project
{
  ControlService::ControlService(VSwitch& vswitch, uint64_t topology_revision) noexcept
      : vswitch_(vswitch), topology_revision_(topology_revision)
  {
  }

  ControlService::ControlService(VSwitch& vswitch, TopologyController& topology_controller) noexcept
      : vswitch_(vswitch), topology_revision_(0), topology_controller_(topology_controller)
  {
  }

  ControlDispatch ControlService::dispatch(std::string_view json)
  {
    const auto request = control_request_from_json(json);
    if (!request)
    {
      return { reject({}, to_string(request.error())), std::nullopt, std::nullopt };
    }

    const auto validation = validate(request.value());
    if (!validation)
    {
      return { reject(request.value().request_id, to_string(validation.error())), std::nullopt, std::nullopt };
    }
    if (request.value().topology_revision != current_topology_revision())
    {
      return { reject(request.value().request_id, "stale topology revision"), std::nullopt, std::nullopt };
    }

    if (request.value().command == ControlCommand::GetSwitchState)
    {
      SwitchMetricsEvent metrics_event;
      metrics_event.event_sequence = next_event_sequence_++;
      metrics_event.topology_revision = current_topology_revision();
      metrics_event.metrics = vswitch_.metrics();

      ControlReply reply;
      reply.request_id = request.value().request_id;
      reply.accepted = true;
      reply.operation_id = "switch-state-" + std::to_string(metrics_event.event_sequence);
      return { std::move(reply), std::move(metrics_event), std::nullopt };
    }

    if (!topology_controller_ ||
        (request.value().command != ControlCommand::SetPortFault &&
         request.value().command != ControlCommand::ClearPortFault &&
         request.value().command != ControlCommand::SetLinkFault &&
         request.value().command != ControlCommand::ClearLinkFault))
    {
      return { reject(request.value().request_id, "command is not implemented"), std::nullopt, std::nullopt };
    }

    auto& controller = topology_controller_->get();
    const auto& fault = request.value().fault;
    bool active = false;
    std::optional<TopologyControllerError> fault_error;
    switch (request.value().command)
    {
      case ControlCommand::SetPortFault:
      {
        const auto result = controller.set_port_fault(fault.port_id, fault.configuration);
        if (!result) fault_error = result.error();
        active = true;
        break;
      }
      case ControlCommand::SetLinkFault:
      {
        const auto result = controller.set_link_fault(fault.first_endpoint, fault.second_endpoint, fault.configuration);
        if (!result) fault_error = result.error();
        active = true;
        break;
      }
      case ControlCommand::ClearPortFault:
        if (!controller.clear_port_fault(fault.port_id))
        {
          return { reject(request.value().request_id, "fault target has no active configuration"), std::nullopt,
                   std::nullopt };
        }
        break;
      case ControlCommand::ClearLinkFault:
        if (!controller.clear_link_fault(fault.first_endpoint, fault.second_endpoint))
        {
          return { reject(request.value().request_id, "fault target has no active configuration"), std::nullopt,
                   std::nullopt };
        }
        break;
      case ControlCommand::GetSwitchState:
      case ControlCommand::StartBenchmark:
      case ControlCommand::StopRun: break;
    }
    if (fault_error)
    {
      return { reject(request.value().request_id, to_string(*fault_error)), std::nullopt, std::nullopt };
    }

    FaultStateEvent fault_event;
    fault_event.event_sequence = next_event_sequence_++;
    fault_event.topology_revision = current_topology_revision();
    fault_event.first_endpoint =
        request.value().command == ControlCommand::SetPortFault || request.value().command == ControlCommand::ClearPortFault
            ? fault.port_id
            : fault.first_endpoint;
    fault_event.second_endpoint = fault.first_endpoint.empty() ? fault.second_endpoint : std::string{};
    fault_event.configuration = fault.configuration;
    fault_event.active = active;

    ControlReply reply;
    reply.request_id = request.value().request_id;
    reply.accepted = true;
    reply.operation_id = std::string(active ? "fault-set-" : "fault-cleared-") +
                         std::to_string(fault_event.event_sequence);
    return { std::move(reply), std::nullopt, std::move(fault_event) };
  }

  uint64_t ControlService::topology_revision() const noexcept
  {
    return current_topology_revision();
  }

  uint64_t ControlService::current_topology_revision() const noexcept
  {
    return topology_controller_ ? topology_controller_->get().topology_revision() : topology_revision_;
  }

  ControlReply ControlService::reject(std::string request_id, std::string error) const
  {
    ControlReply reply;
    reply.request_id = std::move(request_id);
    reply.error = std::move(error);
    return reply;
  }
}
