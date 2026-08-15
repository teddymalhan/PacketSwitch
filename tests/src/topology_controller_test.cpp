#include <chrono>
#include <utility>

#include <gtest/gtest.h>

#include "project/topology_controller.hpp"

namespace
{
  project::Topology make_topology()
  {
    project::TopologyConfiguration configuration;
    configuration.name = "security-lab";
    configuration.nodes = {
      { "client-a", project::TopologyNodeType::Host },
      { "client-b", project::TopologyNodeType::Host },
      { "core-switch", project::TopologyNodeType::Switch },
    };
    configuration.links = {
      { "client-a", "core-switch", std::chrono::milliseconds(1) },
      { "client-b", "core-switch", std::chrono::milliseconds(1) },
    };

    auto topology = project::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  TEST(TopologyControllerTest, RequiresALoadedTopology)
  {
    project::TopologyController controller;
    const auto decision = controller.evaluate_port("client-a", 64, std::chrono::steady_clock::time_point{});

    EXPECT_FALSE(decision.has_value());
    EXPECT_EQ(decision.error(), project::TopologyControllerError::NoTopology);
  }

  TEST(TopologyControllerTest, AppliesFaultsOnlyToTopologyTargets)
  {
    project::TopologyController controller(42);
    controller.load(make_topology());

    project::FaultConfiguration configuration;
    configuration.blackhole = true;

    project::FaultConfiguration invalid_configuration;
    invalid_configuration.loss_basis_points = 10001;
    const auto invalid_fault = controller.set_port_fault("client-b", invalid_configuration);
    EXPECT_FALSE(invalid_fault.has_value());
    EXPECT_EQ(invalid_fault.error(), project::TopologyControllerError::InvalidFaultConfiguration);

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
    project::TopologyController controller(99);
    controller.load(make_topology());

    project::FaultConfiguration configuration;
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

  TEST(TopologyControllerTest, ReportsOnlyPublicTopologyFaultIdentifiers)
  {
    project::TopologyController controller;
    EXPECT_EQ(controller.active_faults().error(), project::TopologyControllerError::NoTopology);

    controller.load(make_topology());
    project::FaultConfiguration port_configuration;
    port_configuration.blackhole = true;
    project::FaultConfiguration link_configuration;
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
