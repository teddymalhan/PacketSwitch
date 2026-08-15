#ifndef PROJECT_TOPOLOGY_HPP_
#define PROJECT_TOPOLOGY_HPP_

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "project/expected.hpp"

namespace project
{
  enum class TopologyNodeType
  {
    Host,
    Switch
  };

  enum class TopologyValidationError
  {
    MissingName,
    MissingNodes,
    MissingNodeId,
    DuplicateNodeId,
    MissingSwitch,
    MultipleSwitches,
    UnknownLinkEndpoint,
    SelfLink,
    DuplicateLink,
    InvalidLinkShape,
    NegativeLatency
  };

  struct TopologyNode
  {
    std::string id;
    TopologyNodeType type = TopologyNodeType::Host;
  };

  struct TopologyLink
  {
    std::string from;
    std::string to;
    std::chrono::milliseconds latency{ 0 };
  };

  struct TopologyConfiguration
  {
    std::string name;
    std::vector<TopologyNode> nodes;
    std::vector<TopologyLink> links;
  };

  class Topology
  {
   public:
    [[nodiscard]] static expected<Topology, TopologyValidationError> create(TopologyConfiguration configuration);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::vector<TopologyNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<TopologyLink>& links() const noexcept;
    [[nodiscard]] const TopologyNode* switch_node() const noexcept;
    [[nodiscard]] size_t port_count() const noexcept;

   private:
    explicit Topology(TopologyConfiguration configuration, size_t switch_index) noexcept;

    TopologyConfiguration configuration_;
    size_t switch_index_ = 0;
  };

  [[nodiscard]] const char* to_string(TopologyNodeType type) noexcept;
  [[nodiscard]] const char* to_string(TopologyValidationError error) noexcept;
}

#endif
