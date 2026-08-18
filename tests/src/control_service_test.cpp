#include "wirelab/control_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{
  wirelab::VSwitch make_switch()
  {
    auto created = wirelab::VSwitch::create(0, wirelab::VSwitchLogLevel::Frame);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "security-lab";
    configuration.nodes = { { "client-a", wirelab::TopologyNodeType::Host },
                            { "client-b", wirelab::TopologyNodeType::Host },
                            { "core-switch", wirelab::TopologyNodeType::Switch } };
    configuration.links = { { "client-a", "core-switch", std::chrono::milliseconds(1) },
                            { "client-b", "core-switch", std::chrono::milliseconds(1) } };
    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  TEST(ControlServiceTest, ReturnsAcknowledgementAndMetricsSnapshot)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 3);

    const auto result =
        service.dispatch(R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":3})");

    EXPECT_TRUE(result.reply.accepted);
    EXPECT_EQ(result.reply.request_id, "state-1");
    EXPECT_EQ(result.reply.operation_id, "switch-state-1");
    ASSERT_TRUE(result.metrics_event.has_value());
    EXPECT_EQ(result.metrics_event->event_sequence, 1U);
    EXPECT_EQ(result.metrics_event->topology_revision, 3U);
    EXPECT_EQ(result.metrics_event->metrics.received_packets, 0U);
  }

  TEST(ControlServiceTest, RejectsInvalidAndStaleRequests)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 3);

    const auto malformed = service.dispatch("not json");
    EXPECT_FALSE(malformed.reply.accepted);
    EXPECT_EQ(malformed.reply.error, "malformed JSON");
    EXPECT_FALSE(malformed.metrics_event.has_value());

    const auto stale =
        service.dispatch(R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":2})");
    EXPECT_FALSE(stale.reply.accepted);
    EXPECT_EQ(stale.reply.request_id, "state-1");
    EXPECT_EQ(stale.reply.error, "stale topology revision");
  }

  // 64 packets in batches of 8, so a slice budget below the total leaves the
  // run unfinished and a client sees progress before a result.
  constexpr const char* kBenchmarkRequest =
      R"({"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":8,"duration_seconds":60,"seed":42,"packets":64,"frame_size":64}})";

  TEST(ControlServiceTest, RunsABenchmarkInSlicesAndPublishesItsResult)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch);

    const auto started = service.dispatch(kBenchmarkRequest);
    ASSERT_TRUE(started.reply.accepted);
    EXPECT_EQ(started.reply.operation_id, "benchmark-1");
    ASSERT_TRUE(started.benchmark_progress_event.has_value());
    EXPECT_EQ(started.benchmark_progress_event->completed_packets, 0U);
    EXPECT_EQ(started.benchmark_progress_event->total_packets, 64U);
    EXPECT_TRUE(service.benchmark_active());

    const auto partial = service.advance_benchmark(16);
    ASSERT_TRUE(partial.progress_event.has_value());
    EXPECT_EQ(partial.progress_event->completed_packets, 16U);
    EXPECT_FALSE(partial.result_event.has_value());
    EXPECT_TRUE(service.benchmark_active());

    const auto finished = service.advance_benchmark(64);
    ASSERT_TRUE(finished.result_event.has_value());
    EXPECT_TRUE(finished.result_event->completed);
    EXPECT_EQ(finished.result_event->operation_id, "benchmark-1");
    EXPECT_EQ(finished.result_event->result.completed_packets, 64U);
    EXPECT_EQ(finished.result_event->result.received_packets, 64U);
    EXPECT_EQ(finished.result_event->result.backend, "cpu");
    // The run is gone once it reported, so the next slice has nothing to say.
    EXPECT_FALSE(service.benchmark_active());
    EXPECT_FALSE(service.advance_benchmark(64).progress_event.has_value());
  }

  TEST(ControlServiceTest, RefusesASecondRunAndReportsWhatAStoppedRunMeasured)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch);

    ASSERT_TRUE(service.dispatch(kBenchmarkRequest).reply.accepted);
    const auto second = service.dispatch(kBenchmarkRequest);
    EXPECT_FALSE(second.reply.accepted);
    EXPECT_EQ(second.reply.error, "a benchmark run is already active");

    EXPECT_EQ(service.advance_benchmark(16).progress_event->completed_packets, 16U);

    const auto stopped =
        service.dispatch(R"({"api_version":1,"request_id":"stop-1","command":"stop_run","topology_revision":0})");
    ASSERT_TRUE(stopped.reply.accepted);
    ASSERT_TRUE(stopped.benchmark_result_event.has_value());
    EXPECT_FALSE(stopped.benchmark_result_event->completed);
    EXPECT_EQ(stopped.benchmark_result_event->result.completed_packets, 16U);
    EXPECT_FALSE(service.benchmark_active());

    const auto nothing_to_stop =
        service.dispatch(R"({"api_version":1,"request_id":"stop-2","command":"stop_run","topology_revision":0})");
    EXPECT_FALSE(nothing_to_stop.reply.accepted);
    EXPECT_EQ(nothing_to_stop.reply.error, "no run is active");
  }

  TEST(ControlServiceTest, RefusesABackendItCannotBuild)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"bench-2","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cuda","batch_size":8,"duration_seconds":60,"seed":42,"packets":64,"frame_size":64}})");

    EXPECT_FALSE(result.reply.accepted);
    EXPECT_EQ(result.reply.error, wirelab::to_string(wirelab::BenchmarkError::UnknownBackend));
    EXPECT_FALSE(service.benchmark_active());
  }

  TEST(ControlServiceTest, RefusesAGeneratorItCannotBuild)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"bench-3","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","generator":"metal","batch_size":8,"duration_seconds":60,"seed":42,"packets":64,"frame_size":64}})");

    EXPECT_FALSE(result.reply.accepted);
    EXPECT_EQ(result.reply.error, wirelab::to_string(wirelab::BenchmarkError::UnknownBackend));
    EXPECT_FALSE(service.benchmark_active());
  }

  TEST(ControlServiceTest, LoadsYamlTopologyAndPublishesRevisionedState)
  {
    const auto path = std::filesystem::temp_directory_path() / "wirelab-control-service-topology.yaml";
    {
      std::ofstream output(path);
      ASSERT_TRUE(output.is_open());
      output << "network:\n"
                "  name: loaded-lab\n"
                "nodes:\n"
                "  - { id: client-a, type: host }\n"
                "  - { id: core-switch, type: switch }\n"
                "links:\n"
                "  - { from: client-a, to: core-switch, latency_ms: 1 }\n";
    }

    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    wirelab::ControlService service(vswitch, controller);

    const auto result = service.dispatch(
        "{\"api_version\":1,\"request_id\":\"topology-1\",\"command\":\"load_topology\",\"topology_revision\":0,"
        "\"parameters\":{\"topology_path\":\"" +
        path.generic_string() + "\"}}");

    ASSERT_TRUE(result.reply.accepted);
    ASSERT_TRUE(result.topology_event.has_value());
    EXPECT_EQ(result.reply.operation_id, "topology-loaded-1");
    EXPECT_EQ(result.topology_event->topology_revision, 1U);
    EXPECT_EQ(result.topology_event->name, "loaded-lab");
    ASSERT_EQ(result.topology_event->nodes.size(), 2U);
    EXPECT_EQ(result.topology_event->nodes[0].id, "client-a");
    ASSERT_EQ(result.topology_event->links.size(), 1U);
    EXPECT_EQ(result.topology_event->links[0].latency, std::chrono::milliseconds(1));
    ASSERT_TRUE(controller.topology());
    EXPECT_EQ(controller.topology()->name(), "loaded-lab");
    EXPECT_TRUE(std::filesystem::remove(path));
  }

  TEST(ControlServiceTest, AppliesFaultCommandsAndPublishesStateChanges)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);

    const auto set = service.dispatch(
        R"({"api_version":1,"request_id":"fault-1","command":"set_port_fault","topology_revision":1,"parameters":{"port_id":"client-a","latency_ms":2,"jitter_ms":1,"loss_basis_points":0,"duplication_basis_points":0,"bandwidth_bits_per_second":0,"blackhole":true,"isolated":false}})");

    ASSERT_TRUE(set.reply.accepted);
    ASSERT_EQ(set.fault_events.size(), 1U);
    EXPECT_EQ(set.reply.operation_id, "fault-set-1");
    EXPECT_EQ(set.fault_events[0].first_endpoint, "client-a");
    EXPECT_TRUE(set.fault_events[0].active);
    EXPECT_TRUE(set.fault_events[0].configuration.blackhole);
    ASSERT_TRUE(controller.active_faults().has_value());
    EXPECT_EQ(controller.active_faults()->size(), 1U);

    const auto clear = service.dispatch(
        R"({"api_version":1,"request_id":"fault-2","command":"clear_port_fault","topology_revision":1,"parameters":{"port_id":"client-a"}})");

    ASSERT_TRUE(clear.reply.accepted);
    ASSERT_EQ(clear.fault_events.size(), 1U);
    EXPECT_EQ(clear.reply.operation_id, "fault-cleared-2");
    EXPECT_FALSE(clear.fault_events[0].active);
    ASSERT_TRUE(controller.active_faults().has_value());
    EXPECT_TRUE(controller.active_faults()->empty());
  }

  TEST(ControlServiceTest, ReturnsEveryActiveFaultWithOrderedStateEvents)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);

    wirelab::FaultConfiguration port_configuration;
    port_configuration.blackhole = true;
    ASSERT_TRUE(controller.set_port_fault("client-b", port_configuration).has_value());
    wirelab::FaultConfiguration link_configuration;
    link_configuration.loss_basis_points = 500;
    ASSERT_TRUE(controller.set_link_fault("client-a", "core-switch", link_configuration).has_value());

    const auto result =
        service.dispatch(R"({"api_version":1,"request_id":"faults-1","command":"get_active_faults","topology_revision":1})");

    ASSERT_TRUE(result.reply.accepted);
    EXPECT_EQ(result.reply.operation_id, "active-faults-1");
    ASSERT_EQ(result.fault_events.size(), 2U);
    EXPECT_EQ(result.fault_events[0].event_sequence, 1U);
    EXPECT_EQ(result.fault_events[0].first_endpoint, "client-a");
    EXPECT_EQ(result.fault_events[0].second_endpoint, "core-switch");
    EXPECT_EQ(result.fault_events[0].configuration.loss_basis_points, 500U);
    EXPECT_EQ(result.fault_events[1].event_sequence, 2U);
    EXPECT_EQ(result.fault_events[1].first_endpoint, "client-b");
    EXPECT_TRUE(result.fault_events[1].second_endpoint.empty());
    EXPECT_TRUE(result.fault_events[1].configuration.blackhole);
  }
  TEST(ControlServiceTest, ReportsALinkFaultWithBothEndpoints)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"link-1","command":"set_link_fault","topology_revision":1,"parameters":{"first_endpoint":"client-a","second_endpoint":"core-switch","latency_ms":7,"jitter_ms":0,"loss_basis_points":0,"duplication_basis_points":0,"bandwidth_bits_per_second":0,"blackhole":false,"isolated":false}})");

    ASSERT_TRUE(result.reply.accepted);
    ASSERT_EQ(result.fault_events.size(), 1U);
    EXPECT_EQ(result.fault_events[0].first_endpoint, "client-a");
    EXPECT_EQ(result.fault_events[0].second_endpoint, "core-switch");
    EXPECT_TRUE(result.fault_events[0].active);
  }

  TEST(ControlServiceTest, PublishesRevisionedAnomalyAndPolicyEventsFromAnalysis)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline({ 1'000'000'000, 1 }, controller);
    const auto now = std::chrono::steady_clock::now();
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    wirelab::AnalysisBatch batch;
    wirelab::PacketAnalysis packet;
    packet.source_mac = wirelab::MacAddress::from_string("00:11:22:33:44:55");
    packet.destination_mac = wirelab::MacAddress::broadcast();
    packet.frame_length = 64;
    packet.ingress_port = 4;
    packet.validity = wirelab::PacketValidity::Valid;
    packet.classification = wirelab::PacketClassification::Broadcast;
    batch.packets = { packet, packet };

    const auto events = service.analysis_events(pipeline.evaluate(batch, 500, now));

    ASSERT_EQ(events.anomaly_events.size(), 1U);
    EXPECT_EQ(events.anomaly_events[0].event_sequence, 1U);
    EXPECT_EQ(events.anomaly_events[0].topology_revision, 1U);
    EXPECT_EQ(events.anomaly_events[0].anomaly.type, wirelab::AnomalyType::BroadcastStorm);
    EXPECT_EQ(events.anomaly_events[0].anomaly.observed_packets, 2U);
    ASSERT_EQ(events.policy_events.size(), 1U);
    EXPECT_EQ(events.policy_events[0].event_sequence, 2U);
    EXPECT_EQ(events.policy_events[0].topology_revision, 1U);
    EXPECT_EQ(events.policy_events[0].decision.rule_name, "contain-broadcast-storm");
    EXPECT_EQ(events.policy_events[0].decision.action, wirelab::PolicyAction::Quarantine);

    // Ingress port 4 is outside this two-host topology, so nothing is enforced.
    ASSERT_EQ(events.enforcement_actions.size(), 1U);
    EXPECT_EQ(events.enforcement_actions[0].outcome, wirelab::EnforcementOutcome::UnknownPort);
    EXPECT_TRUE(events.fault_events.empty());

    const auto repeated = service.analysis_events(pipeline.evaluate(batch, 501, now));
    EXPECT_TRUE(repeated.anomaly_events.empty());
    EXPECT_TRUE(repeated.policy_events.empty());
  }

  TEST(ControlServiceTest, EnforcesAPolicyOntoTheOffendingPortAndPublishesTheFault)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline({ 1'000'000'000, 1 }, controller);
    const auto now = std::chrono::steady_clock::now();
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    wirelab::AnalysisBatch batch;
    wirelab::PacketAnalysis packet;
    packet.source_mac = wirelab::MacAddress::from_string("00:11:22:33:44:55");
    packet.destination_mac = wirelab::MacAddress::broadcast();
    packet.frame_length = 64;
    packet.ingress_port = 1;
    packet.validity = wirelab::PacketValidity::Valid;
    packet.classification = wirelab::PacketClassification::Broadcast;
    batch.packets = { packet, packet };

    const auto events = service.analysis_events(pipeline.evaluate(batch, 500, now));

    ASSERT_EQ(events.enforcement_actions.size(), 1U);
    EXPECT_EQ(events.enforcement_actions[0].port_id, "client-b");
    EXPECT_EQ(events.enforcement_actions[0].kind, wirelab::EnforcementKind::Isolate);
    EXPECT_EQ(events.enforcement_actions[0].outcome, wirelab::EnforcementOutcome::Applied);

    ASSERT_EQ(events.fault_events.size(), 1U);
    EXPECT_EQ(events.fault_events[0].first_endpoint, "client-b");
    EXPECT_TRUE(events.fault_events[0].second_endpoint.empty());
    EXPECT_TRUE(events.fault_events[0].active);
    EXPECT_TRUE(events.fault_events[0].configuration.isolated);

    const auto fault = controller.port_fault("client-b");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->isolated);
  }
}  // namespace
