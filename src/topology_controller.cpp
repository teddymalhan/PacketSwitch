#include "project/topology_controller.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <tuple>
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

  expected<std::vector<TopologyFault>, TopologyControllerError> TopologyController::active_faults() const
  {
    if (!topology_)
    {
      return unexpected(TopologyControllerError::NoTopology);
    }

    const auto configured_faults = fault_engine_.active_faults();
    std::vector<TopologyFault> faults;
    faults.reserve(configured_faults.size());
    for (const auto& node : topology_->nodes())
    {
      if (node.type != TopologyNodeType::Host)
      {
        continue;
      }
      const auto target = port_fault_target(node.id);
      const auto configured = std::find_if(
          configured_faults.begin(), configured_faults.end(),
          [&target](const ActiveFault& fault) { return fault.target == target; });
      if (configured != configured_faults.end())
      {
        faults.push_back({ node.id, {}, configured->configuration });
      }
    }
    for (const auto& link : topology_->links())
    {
      const auto target = link_fault_target(link.from, link.to);
      const auto configured = std::find_if(
          configured_faults.begin(), configured_faults.end(),
          [&target](const ActiveFault& fault) { return fault.target == target; });
      if (configured != configured_faults.end())
      {
        faults.push_back({ link.from, link.to, configured->configuration });
      }
    }
    std::sort(faults.begin(), faults.end(), [](const TopologyFault& left, const TopologyFault& right) {
      return std::tie(left.first_endpoint, left.second_endpoint) < std::tie(right.first_endpoint, right.second_endpoint);
    });
    return faults;
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
    auto decision = fault_engine_.evaluate(link_fault_target(first_endpoint, second_endpoint), frame_bytes, arrival);
    const auto latency = link_latency(first_endpoint, second_endpoint);
    if (latency == std::chrono::milliseconds::zero())
    {
      return decision;
    }

    const auto maximum_delivery_time = std::chrono::steady_clock::time_point::max() - latency;
    for (uint8_t index = 0; index < decision.delivery_count; ++index)
    {
      auto& delivery_time = decision.delivery_times[index];
      delivery_time = delivery_time > maximum_delivery_time
                          ? std::chrono::steady_clock::time_point::max()
                          : delivery_time + latency;
    }
    return decision;
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

  std::chrono::milliseconds TopologyController::link_latency(
      std::string_view first_endpoint, std::string_view second_endpoint) const noexcept
  {
    const auto link = std::find_if(
        topology_->links().begin(), topology_->links().end(), [first_endpoint, second_endpoint](const TopologyLink& link) {
          return (link.from == first_endpoint && link.to == second_endpoint) ||
                 (link.from == second_endpoint && link.to == first_endpoint);
        });
    return link->latency;
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
