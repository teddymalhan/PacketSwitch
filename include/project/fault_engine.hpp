#ifndef PROJECT_FAULT_ENGINE_HPP_
#define PROJECT_FAULT_ENGINE_HPP_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "project/expected.hpp"

namespace project
{
  enum class FaultConfigurationError
  {
    MissingTarget,
    NegativeLatency,
    InvalidLossPercentage,
    InvalidDuplicationPercentage
  };

  struct FaultConfiguration
  {
    std::chrono::nanoseconds latency{ 0 };
    std::chrono::nanoseconds jitter{ 0 };
    uint32_t loss_basis_points = 0;
    uint32_t duplication_basis_points = 0;
    uint64_t bandwidth_bits_per_second = 0;
    bool blackhole = false;
    bool isolated = false;
  };

  struct ActiveFault
  {
    std::string target;
    FaultConfiguration configuration;
  };

  struct FaultDecision
  {
    bool dropped = false;
    uint8_t delivery_count = 0;
    std::array<std::chrono::steady_clock::time_point, 2> delivery_times{};
  };

  class FaultEngine
  {
   public:
    explicit FaultEngine(uint64_t seed = 1) noexcept;

    [[nodiscard]] expected<void, FaultConfigurationError> set_fault(
        std::string target, FaultConfiguration configuration);
    [[nodiscard]] bool clear_fault(std::string_view target);
    [[nodiscard]] bool has_fault(std::string_view target) const;
    [[nodiscard]] std::vector<ActiveFault> active_faults() const;
    [[nodiscard]] FaultDecision evaluate(
        std::string_view target, size_t frame_bytes, std::chrono::steady_clock::time_point arrival);

   private:
    struct FaultState
    {
      FaultConfiguration configuration;
      uint64_t random_state = 0;
      std::chrono::steady_clock::time_point next_transmit_time{};
    };

    [[nodiscard]] static uint64_t target_seed(uint64_t seed, std::string_view target) noexcept;
    [[nodiscard]] static uint64_t next_random(uint64_t& state) noexcept;
    [[nodiscard]] static uint64_t bounded_random(uint64_t& state, uint64_t upper_inclusive) noexcept;
    [[nodiscard]] static std::chrono::nanoseconds transmission_time(size_t frame_bytes, uint64_t bits_per_second) noexcept;
    [[nodiscard]] static expected<void, FaultConfigurationError> validate(
        std::string_view target, const FaultConfiguration& configuration) noexcept;

    uint64_t seed_;
    std::unordered_map<std::string, FaultState> states_;
  };

  [[nodiscard]] const char* to_string(FaultConfigurationError error) noexcept;
}

#endif
