#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

#include <gtest/gtest.h>

#include "project/control_service.hpp"

namespace
{
  project::VSwitch make_switch()
  {
    auto created = project::VSwitch::create(0, project::VSwitchLogLevel::Frame);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  project::Topology make_topology()
  {
    project::TopologyConfiguration configuration;
    configuration.name = "security-lab";
    configuration.nodes = { { "client-a", project::TopologyNodeType::Host },
                            { "client-b", project::TopologyNodeType::Host },
                            { "core-switch", project::TopologyNodeType::Switch } };
    configuration.links = { { "client-a", "core-switch", std::chrono::milliseconds(1) },
                            { "client-b", "core-switch", std::chrono::milliseconds(1) } };
    auto topology = project::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  TEST(ControlServiceTest, ReturnsAcknowledgementAndMetricsSnapshot)
  {
    auto vswitch = make_switch();
    project::ControlService service(vswitch, 3);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":3})");

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
    project::ControlService service(vswitch, 3);

    const auto malformed = service.dispatch("not json");
    EXPECT_FALSE(malformed.reply.accepted);
    EXPECT_EQ(malformed.reply.error, "malformed JSON");
    EXPECT_FALSE(malformed.metrics_event.has_value());

    const auto stale = service.dispatch(
        R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":2})");
    EXPECT_FALSE(stale.reply.accepted);
    EXPECT_EQ(stale.reply.request_id, "state-1");
    EXPECT_EQ(stale.reply.error, "stale topology revision");
  }

  TEST(ControlServiceTest, RejectsCommandsWithoutAnImplementation)
  {
    auto vswitch = make_switch();
    project::ControlService service(vswitch);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":1,"duration_seconds":1,"seed":42}})");

    EXPECT_FALSE(result.reply.accepted);
    EXPECT_EQ(result.reply.error, "command is not implemented");
    EXPECT_FALSE(result.metrics_event.has_value());
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
    project::TopologyController controller;
    project::ControlService service(vswitch, controller);

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
    project::TopologyController controller;
    controller.load(make_topology());
    project::ControlService service(vswitch, controller);

    const auto set = service.dispatch(
        R"({"api_version":1,"request_id":"fault-1","command":"set_port_fault","topology_revision":1,"parameters":{"port_id":"client-a","latency_ms":2,"jitter_ms":1,"loss_basis_points":0,"duplication_basis_points":0,"bandwidth_bits_per_second":0,"blackhole":true,"isolated":false}})");

    ASSERT_TRUE(set.reply.accepted);
    ASSERT_TRUE(set.fault_event.has_value());
    EXPECT_EQ(set.reply.operation_id, "fault-set-1");
    EXPECT_EQ(set.fault_event->first_endpoint, "client-a");
    EXPECT_TRUE(set.fault_event->active);
    EXPECT_TRUE(set.fault_event->configuration.blackhole);
    ASSERT_TRUE(controller.active_faults().has_value());
    EXPECT_EQ(controller.active_faults()->size(), 1U);

    const auto clear = service.dispatch(
        R"({"api_version":1,"request_id":"fault-2","command":"clear_port_fault","topology_revision":1,"parameters":{"port_id":"client-a"}})");

    ASSERT_TRUE(clear.reply.accepted);
    ASSERT_TRUE(clear.fault_event.has_value());
    EXPECT_EQ(clear.reply.operation_id, "fault-cleared-2");
    EXPECT_FALSE(clear.fault_event->active);
    ASSERT_TRUE(controller.active_faults().has_value());
    EXPECT_TRUE(controller.active_faults()->empty());
  }
}
