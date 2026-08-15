#include <gtest/gtest.h>

#include "wirelab/topology.hpp"

namespace
{
  wirelab::TopologyConfiguration valid_topology()
  {
    return {
        "security-lab",
        { { "client-a", wirelab::TopologyNodeType::Host },
          { "client-b", wirelab::TopologyNodeType::Host },
          { "core-switch", wirelab::TopologyNodeType::Switch } },
        { { "client-a", "core-switch", std::chrono::milliseconds(1) },
          { "client-b", "core-switch", std::chrono::milliseconds(2) } } };
  }

  TEST(TopologyTest, CreatesSingleSwitchTopology)
  {
    const auto topology = wirelab::Topology::create(valid_topology());

    ASSERT_TRUE(topology.has_value());
    EXPECT_EQ(topology.value().name(), "security-lab");
    ASSERT_NE(topology.value().switch_node(), nullptr);
    EXPECT_EQ(topology.value().switch_node()->id, "core-switch");
    EXPECT_EQ(topology.value().port_count(), 2U);
    EXPECT_EQ(topology.value().links().at(1).latency, std::chrono::milliseconds(2));
  }

  TEST(TopologyTest, RejectsInvalidNodeSets)
  {
    auto missing_name = valid_topology();
    missing_name.name.clear();
    EXPECT_EQ(wirelab::Topology::create(std::move(missing_name)).error(), wirelab::TopologyValidationError::MissingName);

    auto duplicate_id = valid_topology();
    duplicate_id.nodes.at(1).id = "client-a";
    EXPECT_EQ(wirelab::Topology::create(std::move(duplicate_id)).error(), wirelab::TopologyValidationError::DuplicateNodeId);

    auto no_switch = valid_topology();
    no_switch.nodes.at(2).type = wirelab::TopologyNodeType::Host;
    EXPECT_EQ(wirelab::Topology::create(std::move(no_switch)).error(), wirelab::TopologyValidationError::MissingSwitch);

    auto two_switches = valid_topology();
    two_switches.nodes.at(1).type = wirelab::TopologyNodeType::Switch;
    EXPECT_EQ(wirelab::Topology::create(std::move(two_switches)).error(), wirelab::TopologyValidationError::MultipleSwitches);
  }

  TEST(TopologyTest, RejectsInvalidLinks)
  {
    auto unknown_endpoint = valid_topology();
    unknown_endpoint.links.at(0).to = "unknown";
    EXPECT_EQ(wirelab::Topology::create(std::move(unknown_endpoint)).error(), wirelab::TopologyValidationError::UnknownLinkEndpoint);

    auto duplicate_link = valid_topology();
    duplicate_link.links.push_back({ "core-switch", "client-a", std::chrono::milliseconds(0) });
    EXPECT_EQ(wirelab::Topology::create(std::move(duplicate_link)).error(), wirelab::TopologyValidationError::DuplicateLink);

    auto host_to_host = valid_topology();
    host_to_host.links.at(0).to = "client-b";
    EXPECT_EQ(wirelab::Topology::create(std::move(host_to_host)).error(), wirelab::TopologyValidationError::InvalidLinkShape);

    auto negative_latency = valid_topology();
    negative_latency.links.at(0).latency = std::chrono::milliseconds(-1);
    EXPECT_EQ(
        wirelab::Topology::create(std::move(negative_latency)).error(),
        wirelab::TopologyValidationError::NegativeLatency);
  }
}
