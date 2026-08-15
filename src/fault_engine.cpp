#include "wirelab/fault_engine.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace wirelab
{
  namespace
  {
    constexpr uint32_t BASIS_POINTS_PER_PERCENTAGE = 10000;
    constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
  }

  FaultEngine::FaultEngine(uint64_t seed) noexcept : seed_(seed)
  {
  }

  expected<void, FaultConfigurationError> FaultEngine::set_fault(
      std::string target, FaultConfiguration configuration)
  {
    const auto valid = validate(target, configuration);
    if (!valid)
    {
      return valid;
    }

    FaultState state;
    state.configuration = configuration;
    state.random_state = target_seed(seed_, target);
    states_.insert_or_assign(std::move(target), state);
    return {};
  }

  bool FaultEngine::clear_fault(std::string_view target)
  {
    return states_.erase(std::string(target)) != 0;
  }

  bool FaultEngine::has_fault(std::string_view target) const
  {
    return states_.find(std::string(target)) != states_.end();
  }

  std::vector<ActiveFault> FaultEngine::active_faults() const
  {
    std::vector<ActiveFault> faults;
    faults.reserve(states_.size());
    for (const auto& [target, state] : states_)
    {
      faults.push_back({ target, state.configuration });
    }
    std::sort(faults.begin(), faults.end(), [](const ActiveFault& left, const ActiveFault& right) {
      return left.target < right.target;
    });
    return faults;
  }

  FaultDecision FaultEngine::evaluate(
      std::string_view target, size_t frame_bytes, std::chrono::steady_clock::time_point arrival)
  {
    const auto state_it = states_.find(std::string(target));
    if (state_it == states_.end())
    {
      return FaultDecision{ false, 1, { arrival, {} } };
    }

    auto& state = state_it->second;
    const auto& configuration = state.configuration;
    if (configuration.blackhole || configuration.isolated ||
        bounded_random(state.random_state, BASIS_POINTS_PER_PERCENTAGE - 1) < configuration.loss_basis_points)
    {
      return FaultDecision{ true, 0, {} };
    }

    const uint8_t delivery_count = static_cast<uint8_t>(
        1 + (bounded_random(state.random_state, BASIS_POINTS_PER_PERCENTAGE - 1) <
                 configuration.duplication_basis_points));
    const auto jitter = std::chrono::nanoseconds(
        bounded_random(state.random_state, static_cast<uint64_t>(configuration.jitter.count())));
    const auto earliest_delivery = arrival + configuration.latency + jitter;
    FaultDecision decision;
    decision.delivery_count = delivery_count;

    for (uint8_t index = 0; index < delivery_count; ++index)
    {
      auto delivery_time = earliest_delivery;
      if (configuration.bandwidth_bits_per_second != 0)
      {
        delivery_time = std::max(delivery_time, state.next_transmit_time);
        state.next_transmit_time = delivery_time + transmission_time(frame_bytes, configuration.bandwidth_bits_per_second);
      }
      decision.delivery_times[index] = delivery_time;
    }

    return decision;
  }

  uint64_t FaultEngine::target_seed(uint64_t seed, std::string_view target) noexcept
  {
    uint64_t hash = FNV_OFFSET_BASIS ^ seed;
    for (const auto character : target)
    {
      hash ^= static_cast<unsigned char>(character);
      hash *= FNV_PRIME;
    }
    return hash;
  }

  uint64_t FaultEngine::next_random(uint64_t& state) noexcept
  {
    state += 0x9E3779B97F4A7C15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  uint64_t FaultEngine::bounded_random(uint64_t& state, uint64_t upper_inclusive) noexcept
  {
    if (upper_inclusive == 0)
    {
      return 0;
    }
    return next_random(state) % (upper_inclusive + 1);
  }

  std::chrono::nanoseconds FaultEngine::transmission_time(size_t frame_bytes, uint64_t bits_per_second) noexcept
  {
    const auto bits = static_cast<uint64_t>(frame_bytes) * 8ULL;
    if (bits == 0)
    {
      return std::chrono::nanoseconds(0);
    }
    const auto seconds = bits / bits_per_second;
    const auto remainder = bits % bits_per_second;
    if (seconds > std::numeric_limits<uint64_t>::max() / NANOSECONDS_PER_SECOND)
    {
      return std::chrono::nanoseconds::max();
    }
    const auto nanoseconds = seconds * NANOSECONDS_PER_SECOND +
                             (remainder * NANOSECONDS_PER_SECOND + bits_per_second - 1) / bits_per_second;
    if (nanoseconds > static_cast<uint64_t>(std::chrono::nanoseconds::max().count()))
    {
      return std::chrono::nanoseconds::max();
    }
    return std::chrono::nanoseconds(nanoseconds);
  }

  expected<void, FaultConfigurationError> FaultEngine::validate(
      std::string_view target, const FaultConfiguration& configuration) noexcept
  {
    if (target.empty())
    {
      return unexpected(FaultConfigurationError::MissingTarget);
    }
    if (configuration.latency.count() < 0 || configuration.jitter.count() < 0)
    {
      return unexpected(FaultConfigurationError::NegativeLatency);
    }
    if (configuration.loss_basis_points > BASIS_POINTS_PER_PERCENTAGE)
    {
      return unexpected(FaultConfigurationError::InvalidLossPercentage);
    }
    if (configuration.duplication_basis_points > BASIS_POINTS_PER_PERCENTAGE)
    {
      return unexpected(FaultConfigurationError::InvalidDuplicationPercentage);
    }
    return {};
  }

  const char* to_string(FaultConfigurationError error) noexcept
  {
    switch (error)
    {
      case FaultConfigurationError::MissingTarget:
        return "missing target";
      case FaultConfigurationError::NegativeLatency:
        return "latency and jitter must be non-negative";
      case FaultConfigurationError::InvalidLossPercentage:
        return "loss must not exceed 100 percent";
      case FaultConfigurationError::InvalidDuplicationPercentage:
        return "duplication must not exceed 100 percent";
    }
    return "unknown fault configuration error";
  }
}
