#include "project/control_service.hpp"

#include <utility>

namespace project
{
  ControlService::ControlService(VSwitch& vswitch, uint64_t topology_revision) noexcept
      : vswitch_(vswitch), topology_revision_(topology_revision)
  {
  }

  ControlDispatch ControlService::dispatch(std::string_view json)
  {
    const auto request = control_request_from_json(json);
    if (!request)
    {
      return {reject({}, to_string(request.error())), std::nullopt};
    }

    const auto validation = validate(request.value());
    if (!validation)
    {
      return {reject(request.value().request_id, to_string(validation.error())), std::nullopt};
    }
    if (request.value().topology_revision != topology_revision_)
    {
      return {reject(request.value().request_id, "stale topology revision"), std::nullopt};
    }
    if (request.value().command != ControlCommand::GetSwitchState)
    {
      return {reject(request.value().request_id, "command is not implemented"), std::nullopt};
    }

    SwitchMetricsEvent metrics_event;
    metrics_event.event_sequence = next_event_sequence_++;
    metrics_event.topology_revision = topology_revision_;
    metrics_event.metrics = vswitch_.metrics();

    ControlReply reply;
    reply.request_id = request.value().request_id;
    reply.accepted = true;
    reply.operation_id = "switch-state-" + std::to_string(metrics_event.event_sequence);
    return {std::move(reply), std::move(metrics_event)};
  }

  uint64_t ControlService::topology_revision() const noexcept
  {
    return topology_revision_;
  }

  ControlReply ControlService::reject(std::string request_id, std::string error) const
  {
    ControlReply reply;
    reply.request_id = std::move(request_id);
    reply.error = std::move(error);
    return reply;
  }
}
