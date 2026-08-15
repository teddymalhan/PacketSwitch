#include "wirelab/traffic_generator.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#include "wirelab/ethernet_frame.hpp"

namespace wirelab
{
  namespace
  {
    constexpr uint64_t LCG_MULTIPLIER = 6364136223846793005ULL;
    constexpr uint64_t LCG_INCREMENT = 1442695040888963407ULL;

    uint64_t next_random(uint64_t& state) noexcept
    {
      state = state * LCG_MULTIPLIER + LCG_INCREMENT;
      return state;
    }

    MacAddress host_mac(uint32_t host) noexcept
    {
      return MacAddress({ 0x02,
                          0x57,
                          0x4c,
                          static_cast<uint8_t>((host >> 16) & 0xff),
                          static_cast<uint8_t>((host >> 8) & 0xff),
                          static_cast<uint8_t>(host & 0xff) });
    }

    MacAddress unknown_mac(uint64_t value) noexcept
    {
      return MacAddress({ 0x02,
                          0xfe,
                          static_cast<uint8_t>((value >> 24) & 0xff),
                          static_cast<uint8_t>((value >> 16) & 0xff),
                          static_cast<uint8_t>((value >> 8) & 0xff),
                          static_cast<uint8_t>(value & 0xff) });
    }

    void populate_ipv4_header(std::vector<uint8_t>& payload, uint32_t source_host, uint32_t destination_host) noexcept
    {
      constexpr size_t IPV4_MINIMUM_HEADER_SIZE = 20;
      if (payload.size() < IPV4_MINIMUM_HEADER_SIZE ||
          payload.size() > std::numeric_limits<uint16_t>::max())
      {
        return;
      }

      payload[0] = 0x45;
      const uint16_t total_size = static_cast<uint16_t>(payload.size());
      payload[2] = static_cast<uint8_t>(total_size >> 8U);
      payload[3] = static_cast<uint8_t>(total_size);
      payload[8] = 64;
      payload[9] = 0;
      payload[12] = 10;
      payload[13] = 0;
      payload[14] = static_cast<uint8_t>(source_host >> 8U);
      payload[15] = static_cast<uint8_t>(source_host);
      payload[16] = 10;
      payload[17] = 0;
      payload[18] = static_cast<uint8_t>(destination_host >> 8U);
      payload[19] = static_cast<uint8_t>(destination_host);
    }
  }

  DeterministicTrafficGenerator::DeterministicTrafficGenerator(TrafficGeneratorConfig config)
      : config_(config), state_(config.seed)
  {
    if (config_.frame_size < ETHERNET_HEADER_SIZE)
    {
      throw std::invalid_argument("frame_size must include a complete Ethernet header");
    }
    if (config_.host_count < 2)
    {
      throw std::invalid_argument("host_count must be at least two");
    }
  }

  TrafficScenario DeterministicTrafficGenerator::next_scenario() noexcept
  {
    if (config_.scenario != TrafficScenario::Mixed)
    {
      return config_.scenario;
    }
    return static_cast<TrafficScenario>(sequence_ % 3);
  }

  std::vector<uint8_t> DeterministicTrafficGenerator::next_frame()
  {
    const TrafficScenario scenario = next_scenario();
    const uint32_t source_host = static_cast<uint32_t>(sequence_ % config_.host_count);
    const uint32_t destination_host = (source_host + 1) % config_.host_count;
    const uint64_t random_value = next_random(state_);

    MacAddress destination = host_mac(destination_host);
    if (scenario == TrafficScenario::Broadcast)
    {
      destination = MacAddress::broadcast();
    }
    else if (scenario == TrafficScenario::UnknownUnicast)
    {
      destination = unknown_mac(random_value);
    }

    std::vector<uint8_t> payload(config_.frame_size - ETHERNET_HEADER_SIZE);
    for (auto& byte : payload)
    {
      byte = static_cast<uint8_t>(next_random(state_) >> 56);
    }
    populate_ipv4_header(payload, source_host, destination_host);

    ++sequence_;
    return EthernetFrame(destination, host_mac(source_host), EtherType::IPv4, std::move(payload)).serialize();
  }

  std::vector<std::vector<uint8_t>> DeterministicTrafficGenerator::generate(size_t count)
  {
    std::vector<std::vector<uint8_t>> frames;
    frames.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
      frames.push_back(next_frame());
    }
    return frames;
  }
}
