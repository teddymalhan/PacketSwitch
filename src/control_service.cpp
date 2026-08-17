#include "wirelab/control_service.hpp"

#include <iterator>
#include <utility>

namespace wirelab
{
  ControlService::ControlService(VSwitch& vswitch, uint64_t topology_revision) noexcept
      : vswitch_(vswitch),
        topology_revision_(topology_revision)
  {
  }

  ControlService::ControlService(VSwitch& vswitch, TopologyController& topology_controller) noexcept
      : vswitch_(vswitch),
        topology_revision_(0),
        topology_controller_(topology_controller)
  {
  }

  void ControlService::set_supervision_source(SupervisionSource source)
  {
    supervision_source_ = std::move(source);
  }

  ControlDispatch ControlService::dispatch(std::string_view json)
  {
    const auto request = control_request_from_json(json);
    if (!request)
    {
      return reject({}, to_string(request.error()));
    }

    const auto validation = validate(request.value());
    if (!validation)
    {
      return reject(request.value().request_id, to_string(validation.error()));
    }
    if (request.value().topology_revision != current_topology_revision())
    {
      return reject(request.value().request_id, "stale topology revision");
    }

    if (request.value().command == ControlCommand::GetSwitchState)
    {
      SwitchMetricsEvent metrics_event;
      metrics_event.event_sequence = next_event_sequence_++;
      metrics_event.topology_revision = current_topology_revision();
      metrics_event.metrics = vswitch_.metrics();

      ControlDispatch dispatch;
      dispatch.reply = accept(request.value().request_id, "switch-state-" + std::to_string(metrics_event.event_sequence));
      dispatch.metrics_event = std::move(metrics_event);
      return dispatch;
    }

    if (request.value().command == ControlCommand::GetSupervisionState)
    {
      if (!supervision_source_)
      {
        return reject(request.value().request_id, "supervision state is unavailable");
      }

      auto event = supervision_event(supervision_source_());

      ControlDispatch dispatch;
      dispatch.reply = accept(request.value().request_id, "supervision-state-" + std::to_string(event.event_sequence));
      dispatch.supervision_event = std::move(event);
      return dispatch;
    }

    if (request.value().command == ControlCommand::LoadTopology)
    {
      if (!topology_controller_)
      {
        return reject(request.value().request_id, "topology loading is unavailable");
      }

      const auto configuration = topology_configuration_from_yaml_file(request.value().topology.path);
      if (!configuration)
      {
        return reject(request.value().request_id, to_string(configuration.error()));
      }
      const auto topology = Topology::create(configuration.value());
      if (!topology)
      {
        return reject(request.value().request_id, to_string(topology.error()));
      }

      auto& controller = topology_controller_->get();
      controller.load(topology.value());

      TopologyStateEvent topology_event;
      topology_event.event_sequence = next_event_sequence_++;
      topology_event.topology_revision = controller.topology_revision();
      topology_event.name = topology.value().name();
      topology_event.nodes = topology.value().nodes();
      topology_event.links = topology.value().links();

      ControlDispatch dispatch;
      dispatch.reply =
          accept(request.value().request_id, "topology-loaded-" + std::to_string(topology_event.event_sequence));
      dispatch.topology_event = std::move(topology_event);
      return dispatch;
    }

    if (request.value().command != ControlCommand::GetActiveFaults &&
        request.value().command != ControlCommand::SetPortFault &&
        request.value().command != ControlCommand::ClearPortFault &&
        request.value().command != ControlCommand::SetLinkFault && request.value().command != ControlCommand::ClearLinkFault)
    {
      return reject(request.value().request_id, "command is not implemented");
    }
    if (!topology_controller_)
    {
      return reject(request.value().request_id, "topology fault control is unavailable");
    }

    auto& controller = topology_controller_->get();
    if (request.value().command == ControlCommand::GetActiveFaults)
    {
      const auto faults = controller.active_faults();
      if (!faults)
      {
        return reject(request.value().request_id, to_string(faults.error()));
      }

      ControlDispatch dispatch;
      dispatch.reply = accept(request.value().request_id, "active-faults-" + std::to_string(next_event_sequence_));
      dispatch.fault_events.reserve(faults->size());
      for (const auto& active_fault : faults.value())
      {
        FaultStateEvent event;
        event.event_sequence = next_event_sequence_++;
        event.topology_revision = current_topology_revision();
        event.first_endpoint = active_fault.first_endpoint;
        event.second_endpoint = active_fault.second_endpoint;
        event.configuration = active_fault.configuration;
        event.active = true;
        dispatch.fault_events.push_back(std::move(event));
      }
      return dispatch;
    }

    const auto& fault = request.value().fault;
    bool active = false;
    std::optional<TopologyControllerError> fault_error;
    switch (request.value().command)
    {
      case ControlCommand::SetPortFault:
      {
        const auto result = controller.set_port_fault(fault.port_id, fault.configuration);
        if (!result)
          fault_error = result.error();
        active = true;
        break;
      }
      case ControlCommand::SetLinkFault:
      {
        const auto result = controller.set_link_fault(fault.first_endpoint, fault.second_endpoint, fault.configuration);
        if (!result)
          fault_error = result.error();
        active = true;
        break;
      }
      case ControlCommand::ClearPortFault:
        if (!controller.clear_port_fault(fault.port_id))
        {
          return reject(request.value().request_id, "fault target has no active configuration");
        }
        break;
      case ControlCommand::ClearLinkFault:
        if (!controller.clear_link_fault(fault.first_endpoint, fault.second_endpoint))
        {
          return reject(request.value().request_id, "fault target has no active configuration");
        }
        break;
      case ControlCommand::LoadTopology:
      case ControlCommand::GetSwitchState:
      case ControlCommand::GetActiveFaults:
      case ControlCommand::GetSupervisionState:
      case ControlCommand::StartBenchmark:
      case ControlCommand::StopRun: break;
    }
    if (fault_error)
    {
      return reject(request.value().request_id, to_string(*fault_error));
    }

    FaultStateEvent fault_event;
    fault_event.event_sequence = next_event_sequence_++;
    fault_event.topology_revision = current_topology_revision();
    const bool is_port_fault =
        request.value().command == ControlCommand::SetPortFault || request.value().command == ControlCommand::ClearPortFault;
    fault_event.first_endpoint = is_port_fault ? fault.port_id : fault.first_endpoint;
    fault_event.second_endpoint = is_port_fault ? std::string{} : fault.second_endpoint;
    fault_event.configuration = fault.configuration;
    fault_event.active = active;

    ControlDispatch dispatch;
    dispatch.reply = accept(
        request.value().request_id,
        std::string(active ? "fault-set-" : "fault-cleared-") + std::to_string(fault_event.event_sequence));
    dispatch.fault_events.push_back(std::move(fault_event));
    return dispatch;
  }

  AnalysisEventDispatch ControlService::analysis_events(AnalysisOutcome outcome)
  {
    AnalysisEventDispatch dispatch;
    const uint64_t revision = current_topology_revision();

    dispatch.anomaly_events.reserve(outcome.anomalies.size());
    for (const auto& anomaly : outcome.anomalies)
    {
      dispatch.anomaly_events.push_back({ WIRELAB_CONTROL_API_VERSION, next_event_sequence_++, revision, anomaly });
    }

    dispatch.policy_events.reserve(outcome.decisions.size());
    for (const auto& decision : outcome.decisions)
    {
      dispatch.policy_events.push_back({ WIRELAB_CONTROL_API_VERSION, next_event_sequence_++, revision, decision });
    }

    if (!topology_controller_)
    {
      return dispatch;
    }

    auto& controller = topology_controller_->get();
    auto& released = outcome.released;
    auto& applied = outcome.enforced;

    const auto publish = [this, &dispatch, &controller, revision](const EnforcementAction& action, bool active)
    {
      if (action.port_id.empty())
      {
        return;
      }
      FaultStateEvent event;
      event.event_sequence = next_event_sequence_++;
      event.topology_revision = revision;
      event.first_endpoint = action.port_id;
      event.configuration = controller.port_fault(action.port_id).value_or(FaultConfiguration{});
      event.active = active;
      dispatch.fault_events.push_back(std::move(event));
    };

    for (const auto& action : released)
    {
      publish(action, controller.port_fault(action.port_id).has_value());
    }
    for (const auto& action : applied)
    {
      if (action.outcome == EnforcementOutcome::Applied || action.outcome == EnforcementOutcome::Extended)
      {
        publish(action, true);
      }
    }

    dispatch.enforcement_actions.reserve(released.size() + applied.size());
    dispatch.enforcement_actions.insert(
        dispatch.enforcement_actions.end(),
        std::make_move_iterator(released.begin()),
        std::make_move_iterator(released.end()));
    dispatch.enforcement_actions.insert(
        dispatch.enforcement_actions.end(),
        std::make_move_iterator(applied.begin()),
        std::make_move_iterator(applied.end()));
    return dispatch;
  }

  SupervisionStateEvent ControlService::supervision_event(SupervisionSnapshot snapshot)
  {
    SupervisionStateEvent event;
    event.event_sequence = next_event_sequence_++;
    event.topology_revision = current_topology_revision();
    event.analysed_frames = snapshot.analysed_frames;
    event.blocked_frames = snapshot.blocked_frames;
    event.bindings = std::move(snapshot.bindings);
    return event;
  }

  uint64_t ControlService::topology_revision() const noexcept
  {
    return current_topology_revision();
  }

  uint64_t ControlService::current_topology_revision() const noexcept
  {
    return topology_controller_ ? topology_controller_->get().topology_revision() : topology_revision_;
  }

  ControlReply ControlService::accept(std::string request_id, std::string operation_id) const
  {
    ControlReply reply;
    reply.request_id = std::move(request_id);
    reply.accepted = true;
    reply.topology_revision = current_topology_revision();
    reply.operation_id = std::move(operation_id);
    return reply;
  }

  ControlDispatch ControlService::reject(std::string request_id, std::string error) const
  {
    ControlDispatch dispatch;
    dispatch.reply.request_id = std::move(request_id);
    dispatch.reply.topology_revision = current_topology_revision();
    dispatch.reply.error = std::move(error);
    return dispatch;
  }
}  // namespace wirelab
