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

    if (request.value().command == ControlCommand::StartBenchmark)
    {
      return start_benchmark(request.value());
    }

    if (request.value().command == ControlCommand::StopRun)
    {
      return stop_run(request.value());
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

  void ControlService::set_benchmark_backends(BenchmarkBackendFactory factory)
  {
    benchmark_backends_ = std::move(factory);
  }

  ControlDispatch ControlService::start_benchmark(const ControlRequest& request)
  {
    if (benchmark_run_)
    {
      // One run at a time: two runs would interleave their analysis on the same
      // thread and neither would measure the machine the client asked about.
      return reject(request.request_id, "a benchmark run is already active");
    }

    const auto scenario = traffic_scenario_from_string(request.benchmark.scenario);
    if (!scenario)
    {
      return reject(request.request_id, to_string(scenario.error()));
    }

    BenchmarkConfig config;
    config.traffic.scenario = scenario.value();
    config.traffic.seed = request.benchmark.seed;
    config.traffic.frame_size = request.benchmark.frame_size;
    config.packet_count = static_cast<size_t>(request.benchmark.packet_count);
    config.batch_size = request.benchmark.batch_size;
    config.backend = to_string(request.benchmark.backend);

    auto run = benchmark_backends_ ? BenchmarkRun::create(std::move(config), benchmark_backends_)
                                   : BenchmarkRun::create(std::move(config));
    if (!run)
    {
      return reject(request.request_id, to_string(run.error()));
    }

    benchmark_run_ = std::move(run.value());
    benchmark_operation_id_ = "benchmark-" + std::to_string(next_benchmark_id_++);
    benchmark_deadline_ns_ = static_cast<uint64_t>(request.benchmark.duration_seconds) * 1'000'000'000ULL;

    ControlDispatch dispatch;
    dispatch.reply = accept(request.request_id, benchmark_operation_id_);
    // The run has measured nothing yet; the zero-progress event tells a client
    // which operation it is now following and how much work that operation is.
    dispatch.benchmark_progress_event = progress_event();
    return dispatch;
  }

  ControlDispatch ControlService::stop_run(const ControlRequest& request)
  {
    if (!benchmark_run_)
    {
      return reject(request.request_id, "no run is active");
    }

    ControlDispatch dispatch;
    dispatch.reply = accept(request.request_id, benchmark_operation_id_);
    // A stopped run still reports what it measured before it was stopped: the
    // partial numbers are why a client stops a run rather than abandoning it.
    dispatch.benchmark_result_event = result_event(false);
    benchmark_run_.reset();
    return dispatch;
  }

  BenchmarkEventDispatch ControlService::advance_benchmark(size_t max_packets)
  {
    BenchmarkEventDispatch dispatch;
    if (!benchmark_run_)
    {
      return dispatch;
    }

    const size_t advanced = benchmark_run_->advance(max_packets);
    if (advanced == 0 && !benchmark_run_->finished())
    {
      return dispatch;
    }

    dispatch.progress_event = progress_event();
    const bool out_of_time = benchmark_deadline_ns_ != 0 && benchmark_run_->result().elapsed_ns >= benchmark_deadline_ns_;
    if (benchmark_run_->finished() || out_of_time)
    {
      // A duration-limited run that ran out of time completed the work it was
      // given, so it is reported as completed rather than as a stop.
      dispatch.result_event = result_event(true);
      benchmark_run_.reset();
    }
    return dispatch;
  }

  bool ControlService::benchmark_active() const noexcept
  {
    return benchmark_run_.has_value();
  }

  BenchmarkProgressEvent ControlService::progress_event()
  {
    BenchmarkProgressEvent event;
    event.event_sequence = next_event_sequence_++;
    event.topology_revision = current_topology_revision();
    event.operation_id = benchmark_operation_id_;
    event.completed_packets = benchmark_run_->completed_packets();
    event.total_packets = benchmark_run_->total_packets();
    return event;
  }

  BenchmarkResultEvent ControlService::result_event(bool completed)
  {
    BenchmarkResultEvent event;
    event.event_sequence = next_event_sequence_++;
    event.topology_revision = current_topology_revision();
    event.operation_id = benchmark_operation_id_;
    event.completed = completed;
    event.result = benchmark_run_->result();
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
