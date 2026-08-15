#include "project/topology.hpp"

#include <unordered_set>
#include <utility>

namespace project
{
  namespace
  {
    std::string link_key(const std::string& first, const std::string& second)
    {
      return first < second ? first + '\0' + second : second + '\0' + first;
    }
  }

  const char* to_string(TopologyNodeType type) noexcept
  {
    switch (type)
    {
      case TopologyNodeType::Host: return "host";
      case TopologyNodeType::Switch: return "switch";
    }
    return "unknown";
  }

  const char* to_string(TopologyValidationError error) noexcept
  {
    switch (error)
    {
      case TopologyValidationError::MissingName: return "topology name is required";
      case TopologyValidationError::MissingNodes: return "topology requires at least one node";
      case TopologyValidationError::MissingNodeId: return "topology node id is required";
      case TopologyValidationError::DuplicateNodeId: return "topology node ids must be unique";
      case TopologyValidationError::MissingSwitch: return "topology requires exactly one switch";
      case TopologyValidationError::MultipleSwitches: return "topology requires exactly one switch";
      case TopologyValidationError::UnknownLinkEndpoint: return "topology link endpoint does not exist";
      case TopologyValidationError::SelfLink: return "topology link cannot connect a node to itself";
      case TopologyValidationError::DuplicateLink: return "topology links must be unique";
      case TopologyValidationError::InvalidLinkShape: return "topology links must connect the switch to a host";
      case TopologyValidationError::NegativeLatency: return "topology link latency cannot be negative";
    }
    return "unknown topology validation error";
  }

  expected<Topology, TopologyValidationError> Topology::create(TopologyConfiguration configuration)
  {
    if (configuration.name.empty())
    {
      return unexpected(TopologyValidationError::MissingName);
    }
    if (configuration.nodes.empty())
    {
      return unexpected(TopologyValidationError::MissingNodes);
    }

    std::unordered_set<std::string> node_ids;
    size_t switch_count = 0;
    size_t switch_index = 0;
    for (size_t index = 0; index < configuration.nodes.size(); ++index)
    {
      const auto& node = configuration.nodes[index];
      if (node.id.empty())
      {
        return unexpected(TopologyValidationError::MissingNodeId);
      }
      if (!node_ids.insert(node.id).second)
      {
        return unexpected(TopologyValidationError::DuplicateNodeId);
      }
      if (node.type == TopologyNodeType::Switch)
      {
        ++switch_count;
        switch_index = index;
      }
    }
    if (switch_count == 0)
    {
      return unexpected(TopologyValidationError::MissingSwitch);
    }
    if (switch_count > 1)
    {
      return unexpected(TopologyValidationError::MultipleSwitches);
    }

    std::unordered_set<std::string> link_keys;
    for (const auto& link : configuration.links)
    {
      if (link.from == link.to)
      {
        return unexpected(TopologyValidationError::SelfLink);
      }
      if (node_ids.find(link.from) == node_ids.end() || node_ids.find(link.to) == node_ids.end())
      {
        return unexpected(TopologyValidationError::UnknownLinkEndpoint);
      }
      if (!link_keys.insert(link_key(link.from, link.to)).second)
      {
        return unexpected(TopologyValidationError::DuplicateLink);
      }

      if (link.latency.count() < 0)
      {
        return unexpected(TopologyValidationError::NegativeLatency);
      }

      const bool from_is_switch = link.from == configuration.nodes[switch_index].id;
      const bool to_is_switch = link.to == configuration.nodes[switch_index].id;
      if (from_is_switch == to_is_switch)
      {
        return unexpected(TopologyValidationError::InvalidLinkShape);
      }
    }

    return Topology(std::move(configuration), switch_index);
  }

  Topology::Topology(TopologyConfiguration configuration, size_t switch_index) noexcept
      : configuration_(std::move(configuration)), switch_index_(switch_index)
  {
  }

  const std::string& Topology::name() const noexcept
  {
    return configuration_.name;
  }

  const std::vector<TopologyNode>& Topology::nodes() const noexcept
  {
    return configuration_.nodes;
  }

  const std::vector<TopologyLink>& Topology::links() const noexcept
  {
    return configuration_.links;
  }

  const TopologyNode* Topology::switch_node() const noexcept
  {
    return &configuration_.nodes[switch_index_];
  }

  size_t Topology::port_count() const noexcept
  {
    return configuration_.links.size();
  }
}
