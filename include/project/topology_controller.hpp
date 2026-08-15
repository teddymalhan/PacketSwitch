#ifndef PROJECT_TOPOLOGY_CONTROLLER_HPP_
#define PROJECT_TOPOLOGY_CONTROLLER_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "project/expected.hpp"
#include "project/fault_engine.hpp"
#include "project/topology.hpp"

namespace project
{
  enum class TopologyControllerError
  {
    NoTopology,
    UnknownPort,
    UnknownLink,
    InvalidFaultConfiguration
  };
  struct TopologyFault
  {
    std::string first_endpoint;
    std::string second_endpoint;
    FaultConfiguration configuration;
  };

  class TopologyController
  {
   public:
    explicit TopologyController(uint64_t fault_seed = 1) noexcept;

    void load(Topology topology) noexcept;
    [[nodiscard]] bool has_topology() const noexcept;
    [[nodiscard]] const Topology* topology() const noexcept;
    [[nodiscard]] uint64_t topology_revision() const noexcept;

    [[nodiscard]] expected<void, TopologyControllerError> set_port_fault(
        std::string_view port_id, FaultConfiguration configuration);
    [[nodiscard]] expected<void, TopologyControllerError> set_link_fault(
        std::string_view first_endpoint, std::string_view second_endpoint, FaultConfiguration configuration);
    [[nodiscard]] bool clear_port_fault(std::string_view port_id);
    [[nodiscard]] bool clear_link_fault(std::string_view first_endpoint, std::string_view second_endpoint);
    [[nodiscard]] expected<std::vector<TopologyFault>, TopologyControllerError> active_faults() const;

    [[nodiscard]] expected<FaultDecision, TopologyControllerError> evaluate_port(
        std::string_view port_id, size_t frame_bytes, std::chrono::steady_clock::time_point arrival);
    [[nodiscard]] expected<FaultDecision, TopologyControllerError> evaluate_link(
        std::string_view first_endpoint, std::string_view second_endpoint, size_t frame_bytes,
        std::chrono::steady_clock::time_point arrival);

   private:
    [[nodiscard]] bool is_port(std::string_view port_id) const noexcept;
    [[nodiscard]] bool is_link(std::string_view first_endpoint, std::string_view second_endpoint) const noexcept;
    [[nodiscard]] static std::string port_fault_target(std::string_view port_id);
    [[nodiscard]] static std::string link_fault_target(
        std::string_view first_endpoint, std::string_view second_endpoint);

    uint64_t fault_seed_;
    uint64_t topology_revision_ = 0;
    std::optional<Topology> topology_;
    FaultEngine fault_engine_;
  };

  [[nodiscard]] const char* to_string(TopologyControllerError error) noexcept;
}

#endif
