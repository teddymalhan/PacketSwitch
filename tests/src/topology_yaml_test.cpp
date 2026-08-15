#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "project/topology.hpp"

namespace
{
  constexpr const char* VALID_TOPOLOGY = R"yaml(
network:
  name: security-lab
nodes:
  - { id: client-a, type: host }
  -
    id: client-b
    type: host
  - { id: core-switch, type: switch }
links:
  - { from: client-a, to: core-switch, latency_ms: 1 }
  -
    from: client-b
    to: core-switch
    latency_ms: 2 # deterministic link delay
)yaml";

  TEST(TopologyYamlTest, LoadsFlowAndBlockStyleTopology)
  {
    const auto configuration = project::topology_configuration_from_yaml(VALID_TOPOLOGY);

    ASSERT_TRUE(configuration.has_value());
    EXPECT_EQ(configuration.value().name, "security-lab");
    ASSERT_EQ(configuration.value().nodes.size(), 3U);
    EXPECT_EQ(configuration.value().nodes.at(1).id, "client-b");
    EXPECT_EQ(configuration.value().nodes.at(2).type, project::TopologyNodeType::Switch);
    ASSERT_EQ(configuration.value().links.size(), 2U);
    EXPECT_EQ(configuration.value().links.at(1).latency, std::chrono::milliseconds(2));

    const auto topology = project::Topology::create(configuration.value());
    ASSERT_TRUE(topology.has_value());
    EXPECT_EQ(topology.value().port_count(), 2U);
  }

  TEST(TopologyYamlTest, RejectsUnsupportedAndMalformedFields)
  {
    EXPECT_EQ(
        project::topology_configuration_from_yaml("network:\n  name: lab\npolicies: []\n").error(),
        project::TopologyYamlError::InvalidTopLevelKey);
    EXPECT_EQ(
        project::topology_configuration_from_yaml("network:\n  name: lab\nnodes:\n  - { id: a, type: router }\nlinks:\n").error(),
        project::TopologyYamlError::InvalidNodeType);
    EXPECT_EQ(
        project::topology_configuration_from_yaml("network:\n  name: lab\nnodes:\nlinks:\n  - { from: a, to: b, latency_ms: -1 }\n").error(),
        project::TopologyYamlError::InvalidLatency);
    EXPECT_EQ(
        project::topology_configuration_from_yaml("network:\n  name: lab\nnodes:\n  - { id: a, type: host, role: client }\nlinks:\n").error(),
        project::TopologyYamlError::InvalidNodeField);
  }

  TEST(TopologyYamlTest, ReadsTopologyFromFile)
  {
    const auto path = std::filesystem::temp_directory_path() / "wirelab-topology-test.yaml";
    {
      std::ofstream file(path);
      ASSERT_TRUE(file);
      file << VALID_TOPOLOGY;
    }

    const auto configuration = project::topology_configuration_from_yaml_file(path.string());
    std::filesystem::remove(path);

    ASSERT_TRUE(configuration.has_value());
    EXPECT_EQ(configuration.value().name, "security-lab");
    EXPECT_EQ(
        project::topology_configuration_from_yaml_file("missing-wirelab-topology.yaml").error(),
        project::TopologyYamlError::FileRead);
  }
}
