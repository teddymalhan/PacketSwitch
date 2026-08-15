#include <chrono>
#include <utility>

#include <gtest/gtest.h>

#include "wirelab/topology_controller.hpp"

namespace
{
  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "security-lab";
    configuration.nodes = {
      { "client-a", wirelab::TopologyNodeType::Host },
      { "client-b", wirelab::TopologyNodeType::Host },
      { "core-switch", wirelab::TopologyNodeType::Switch },
    };
    configuration.links = {
      { "client-a", "core-switch", std::chrono::milliseconds(1) },
      { "client-b", "core-switch", std::chrono::milliseconds(1) },
    };

    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  TEST(TopologyControllerTest, RequiresALoadedTopology)
  {
    wirelab::TopologyController controller;
    const auto decision = controller.evaluate_port("client-a", 64, std::chrono::steady_clock::time_point{});

    EXPECT_FALSE(decision.has_value());
    EXPECT_EQ(decision.error(), wirelab::TopologyControllerError::NoTopology);
  }

  TEST(TopologyControllerTest, AppliesFaultsOnlyToTopologyTargets)
  {
    wirelab::TopologyController controller(42);
    controller.load(make_topology());

    wirelab::FaultConfiguration configuration;
    configuration.blackhole = true;

    wirelab::FaultConfiguration invalid_configuration;
    invalid_configuration.loss_basis_points = 10001;
    const auto invalid_fault = controller.set_port_fault("client-b", invalid_configuration);
    EXPECT_FALSE(invalid_fault.has_value());
    EXPECT_EQ(invalid_fault.error(), wirelab::TopologyControllerError::InvalidFaultConfiguration);

    EXPECT_EQ(controller.topology_revision(), 1U);
    EXPECT_TRUE(controller.set_port_fault("client-a", configuration).has_value());
    EXPECT_FALSE(controller.set_port_fault("core-switch", configuration).has_value());
    EXPECT_FALSE(controller.set_link_fault("client-a", "client-b", configuration).has_value());

    const auto port_decision = controller.evaluate_port("client-a", 64, std::chrono::steady_clock::time_point{});
    ASSERT_TRUE(port_decision.has_value());
    EXPECT_TRUE(port_decision->dropped);

    const auto link_decision = controller.evaluate_link(
        "core-switch", "client-b", 64, std::chrono::steady_clock::time_point{});
    ASSERT_TRUE(link_decision.has_value());
    EXPECT_FALSE(link_decision->dropped);
    EXPECT_EQ(link_decision->delivery_count, 1U);
  }

  TEST(TopologyControllerTest, LinkFaultsAreDirectionIndependentAndClearedOnReload)
  {
    wirelab::TopologyController controller(99);
    controller.load(make_topology());

    wirelab::FaultConfiguration configuration;
    configuration.duplication_basis_points = 10000;
    ASSERT_TRUE(controller.set_link_fault("client-a", "core-switch", configuration).has_value());

    const auto duplicated = controller.evaluate_link(
        "core-switch", "client-a", 128, std::chrono::steady_clock::time_point{});
    ASSERT_TRUE(duplicated.has_value());
    EXPECT_EQ(duplicated->delivery_count, 2U);

    EXPECT_TRUE(controller.clear_link_fault("core-switch", "client-a"));
    const auto restored = controller.evaluate_link(
        "client-a", "core-switch", 128, std::chrono::steady_clock::time_point{});
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->delivery_count, 1U);

    ASSERT_TRUE(controller.set_link_fault("client-a", "core-switch", configuration).has_value());
    controller.load(make_topology());
    EXPECT_EQ(controller.topology_revision(), 2U);
    const auto reloaded = controller.evaluate_link(
        "client-a", "core-switch", 128, std::chrono::steady_clock::time_point{});
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->delivery_count, 1U);
  }

  TEST(TopologyControllerTest, PreservesTopologyLinkLatencyAlongsideInjectedFaults)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    const auto arrival = std::chrono::steady_clock::time_point(std::chrono::seconds(5));

    const auto baseline = controller.evaluate_link("client-a", "core-switch", 64, arrival);
    ASSERT_TRUE(baseline.has_value());
    ASSERT_EQ(baseline->delivery_count, 1U);
    EXPECT_EQ(baseline->delivery_times[0], arrival + std::chrono::milliseconds(1));

    wirelab::FaultConfiguration configuration;
    configuration.latency = std::chrono::milliseconds(2);
    ASSERT_TRUE(controller.set_link_fault("core-switch", "client-a", configuration).has_value());

    const auto injected = controller.evaluate_link("client-a", "core-switch", 64, arrival);
    ASSERT_TRUE(injected.has_value());
    ASSERT_EQ(injected->delivery_count, 1U);
    EXPECT_EQ(injected->delivery_times[0], arrival + std::chrono::milliseconds(3));

    ASSERT_TRUE(controller.clear_link_fault("client-a", "core-switch"));
    const auto restored = controller.evaluate_link("core-switch", "client-a", 64, arrival);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->delivery_count, 1U);
    EXPECT_EQ(restored->delivery_times[0], arrival + std::chrono::milliseconds(1));
  }

  TEST(TopologyControllerTest, ReportsOnlyPublicTopologyFaultIdentifiers)
  {
    wirelab::TopologyController controller;
    EXPECT_EQ(controller.active_faults().error(), wirelab::TopologyControllerError::NoTopology);

    controller.load(make_topology());
    wirelab::FaultConfiguration port_configuration;
    port_configuration.blackhole = true;
    wirelab::FaultConfiguration link_configuration;
    link_configuration.loss_basis_points = 500;
    ASSERT_TRUE(controller.set_port_fault("client-b", port_configuration).has_value());
    ASSERT_TRUE(controller.set_link_fault("client-a", "core-switch", link_configuration).has_value());

    const auto faults = controller.active_faults();
    ASSERT_TRUE(faults.has_value());
    ASSERT_EQ(faults->size(), 2U);
    EXPECT_EQ((*faults)[0].first_endpoint, "client-a");
    EXPECT_EQ((*faults)[0].second_endpoint, "core-switch");
    EXPECT_EQ((*faults)[0].configuration.loss_basis_points, 500U);
    EXPECT_EQ((*faults)[1].first_endpoint, "client-b");
    EXPECT_TRUE((*faults)[1].second_endpoint.empty());
    EXPECT_TRUE((*faults)[1].configuration.blackhole);
  }
}
