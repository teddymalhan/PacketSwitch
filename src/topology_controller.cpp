#include "project/topology_controller.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace project
{
  TopologyController::TopologyController(uint64_t fault_seed) noexcept
      : fault_seed_(fault_seed), fault_engine_(fault_seed)
  {
  }

  void TopologyController::load(Topology topology) noexcept
  {
    topology_ = std::move(topology);
    fault_engine_ = FaultEngine(fault_seed_);
    if (topology_revision_ != std::numeric_limits<uint64_t>::max())
    {
      ++topology_revision_;
    }
  }

  bool TopologyController::has_topology() const noexcept
  {
    return topology_.has_value();
  }

  const Topology* TopologyController::topology() const noexcept
  {
    return topology_ ? &*topology_ : nullptr;
  }

  uint64_t TopologyController::topology_revision() const noexcept
  {
    return topology_revision_;
  }

  expected<void, TopologyControllerError> TopologyController::set_port_fault(
      std::string_view port_id, FaultConfiguration configuration)
  {
    if (!topology_)
    {
      return unexpected(TopologyControllerError::NoTopology);
    }
    if (!is_port(port_id))
    {
      return unexpected(TopologyControllerError::UnknownPort);
    }
    if (!fault_engine_.set_fault(port_fault_target(port_id), configuration))
    {
      return unexpected(TopologyControllerError::InvalidFaultConfiguration);
    }
    return {};
  }

  expected<void, TopologyControllerError> TopologyController::set_link_fault(
      std::string_view first_endpoint, std::string_view second_endpoint, FaultConfiguration configuration)
  {
    if (!topology_)
    {
      return unexpected(TopologyControllerError::NoTopology);
    }
    if (!is_link(first_endpoint, second_endpoint))
    {
      return unexpected(TopologyControllerError::UnknownLink);
    }
    if (!fault_engine_.set_fault(link_fault_target(first_endpoint, second_endpoint), configuration))
    {
      return unexpected(TopologyControllerError::InvalidFaultConfiguration);
    }
    return {};
  }

  bool TopologyController::clear_port_fault(std::string_view port_id)
  {
    return topology_ && is_port(port_id) && fault_engine_.clear_fault(port_fault_target(port_id));
  }

  bool TopologyController::clear_link_fault(std::string_view first_endpoint, std::string_view second_endpoint)
  {
    return topology_ && is_link(first_endpoint, second_endpoint) &&
           fault_engine_.clear_fault(link_fault_target(first_endpoint, second_endpoint));
  }

  expected<FaultDecision, TopologyControllerError> TopologyController::evaluate_port(
      std::string_view port_id, size_t frame_bytes, std::chrono::steady_clock::time_point arrival)
  {
    if (!topology_)
    {
      return unexpected(TopologyControllerError::NoTopology);
    }
    if (!is_port(port_id))
    {
      return unexpected(TopologyControllerError::UnknownPort);
    }
    return fault_engine_.evaluate(port_fault_target(port_id), frame_bytes, arrival);
  }

  expected<FaultDecision, TopologyControllerError> TopologyController::evaluate_link(
      std::string_view first_endpoint, std::string_view second_endpoint, size_t frame_bytes,
      std::chrono::steady_clock::time_point arrival)
  {
    if (!topology_)
    {
      return unexpected(TopologyControllerError::NoTopology);
    }
    if (!is_link(first_endpoint, second_endpoint))
    {
      return unexpected(TopologyControllerError::UnknownLink);
    }
    return fault_engine_.evaluate(link_fault_target(first_endpoint, second_endpoint), frame_bytes, arrival);
  }

  bool TopologyController::is_port(std::string_view port_id) const noexcept
  {
    return std::any_of(topology_->nodes().begin(), topology_->nodes().end(), [port_id](const TopologyNode& node) {
      return node.id == port_id && node.type == TopologyNodeType::Host;
    });
  }

  bool TopologyController::is_link(std::string_view first_endpoint, std::string_view second_endpoint) const noexcept
  {
    return std::any_of(topology_->links().begin(), topology_->links().end(),
                       [first_endpoint, second_endpoint](const TopologyLink& link) {
                         return (link.from == first_endpoint && link.to == second_endpoint) ||
                                (link.from == second_endpoint && link.to == first_endpoint);
                       });
  }

  std::string TopologyController::port_fault_target(std::string_view port_id)
  {
    return "port:" + std::to_string(port_id.size()) + ":" + std::string(port_id);
  }

  std::string TopologyController::link_fault_target(
      std::string_view first_endpoint, std::string_view second_endpoint)
  {
    if (second_endpoint < first_endpoint)
    {
      std::swap(first_endpoint, second_endpoint);
    }
    return "link:" + std::to_string(first_endpoint.size()) + ":" + std::string(first_endpoint) + ":" +
           std::to_string(second_endpoint.size()) + ":" + std::string(second_endpoint);
  }

  const char* to_string(TopologyControllerError error) noexcept
  {
    switch (error)
    {
      case TopologyControllerError::NoTopology:
        return "no topology is loaded";
      case TopologyControllerError::UnknownPort:
        return "unknown topology port";
      case TopologyControllerError::UnknownLink:
        return "unknown topology link";
      case TopologyControllerError::InvalidFaultConfiguration:
        return "invalid fault configuration";
    }
    return "unknown topology controller error";
  }
}
