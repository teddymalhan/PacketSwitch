#include "wirelab/traffic_generator.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "wirelab/ethernet_frame.hpp"

namespace wirelab
{
  namespace
  {
    constexpr uint64_t SPLITMIX_GAMMA = 0x9E3779B97F4A7C15ULL;
    constexpr size_t IPV4_HEADER_SIZE = 20;
    constexpr size_t UDP_HEADER_SIZE = 8;
    constexpr uint8_t UDP_PROTOCOL = 17;
    // The three original scenarios carry no transport header, so the analyzer
    // sees a valid IPv4 packet that no transport rule keys on.
    constexpr uint8_t NO_TRANSPORT_PROTOCOL = 0;
    constexpr uint8_t IPV4_TIME_TO_LIVE = 64;
    constexpr uint32_t FLOOD_SOURCE_HOST = 0;
    constexpr uint32_t FLOOD_DESTINATION_HOST = 1;
    constexpr uint16_t UDP_FLOOD_DESTINATION_PORT = 9000;
    constexpr uint16_t EPHEMERAL_PORT_BASE = 0xC000;
    constexpr uint16_t EPHEMERAL_PORT_MASK = 0x3FFF;
    constexpr uint16_t PORT_SCAN_SOURCE_PORT = 44444;
    constexpr uint16_t PORT_SCAN_FIRST_PORT = 1;
    constexpr uint64_t PORT_SCAN_PORT_SPAN = 1024;
    // NetBIOS name service, the protocol behind the textbook broadcast storm.
    constexpr uint16_t BROADCAST_STORM_PORT = 137;
    constexpr uint32_t BROADCAST_STORM_SOURCE_LIMIT = 3;
    // 10.0.255.255, the subnet broadcast address a storm is addressed to.
    constexpr uint32_t BROADCAST_STORM_DESTINATION_HOST = 0xffff;

    uint64_t splitmix64(uint64_t& state) noexcept
    {
      uint64_t value = (state += SPLITMIX_GAMMA);
      value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
      value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
      return value ^ (value >> 31U);
    }

    MacAddress host_mac(uint32_t host) noexcept
    {
      return MacAddress(
          { 0x02,
            0x57,
            0x4c,
            static_cast<uint8_t>((host >> 16) & 0xff),
            static_cast<uint8_t>((host >> 8) & 0xff),
            static_cast<uint8_t>(host & 0xff) });
    }

    MacAddress unknown_mac(uint64_t value) noexcept
    {
      return MacAddress(
          { 0x02,
            0xfe,
            static_cast<uint8_t>((value >> 24) & 0xff),
            static_cast<uint8_t>((value >> 16) & 0xff),
            static_cast<uint8_t>((value >> 8) & 0xff),
            static_cast<uint8_t>(value & 0xff) });
    }

    void write_network_u16(uint8_t* out, uint16_t value) noexcept
    {
      out[0] = static_cast<uint8_t>(value >> 8U);
      out[1] = static_cast<uint8_t>(value);
    }

    // Eight bytes per draw rather than one, so a GPU thread that reproduces a
    // frame runs an eighth of the mixing work. Bytes come out least significant
    // first; that ordering is the parity contract with the Metal generator.
    void fill_random_payload(uint64_t& state, uint8_t* payload, size_t size) noexcept
    {
      size_t index = 0;
      while (index + sizeof(uint64_t) <= size)
      {
        uint64_t value = splitmix64(state);
        for (size_t offset = 0; offset < sizeof(uint64_t); ++offset)
        {
          payload[index + offset] = static_cast<uint8_t>(value);
          value >>= 8U;
        }
        index += sizeof(uint64_t);
      }
      if (index < size)
      {
        uint64_t value = splitmix64(state);
        for (; index < size; ++index)
        {
          payload[index] = static_cast<uint8_t>(value);
          value >>= 8U;
        }
      }
    }

    uint16_t ipv4_checksum(const uint8_t* header) noexcept
    {
      uint32_t sum = 0;
      for (size_t index = 0; index < IPV4_HEADER_SIZE; index += 2)
      {
        sum += static_cast<uint32_t>(header[index]) << 8U | header[index + 1];
      }
      while ((sum >> 16U) != 0)
      {
        sum = (sum & 0xffffU) + (sum >> 16U);
      }
      return static_cast<uint16_t>(~sum);
    }

    void write_ipv4_header(
        uint8_t* payload,
        size_t payload_size,
        uint16_t identification,
        uint8_t protocol,
        uint32_t source_host,
        uint32_t destination_host) noexcept
    {
      payload[0] = 0x45;
      payload[1] = 0;
      write_network_u16(payload + 2, static_cast<uint16_t>(payload_size));
      write_network_u16(payload + 4, identification);
      // Zero flags and fragment offset: a non-zero offset makes the analyzer
      // stop before the transport header, which would hide the ports.
      write_network_u16(payload + 6, 0);
      payload[8] = IPV4_TIME_TO_LIVE;
      payload[9] = protocol;
      write_network_u16(payload + 10, 0);
      payload[12] = 10;
      payload[13] = 0;
      write_network_u16(payload + 14, static_cast<uint16_t>(source_host));
      payload[16] = 10;
      payload[17] = 0;
      write_network_u16(payload + 18, static_cast<uint16_t>(destination_host));
      write_network_u16(payload + 10, ipv4_checksum(payload));
    }

    void
    write_udp_header(uint8_t* transport, size_t transport_size, uint16_t source_port, uint16_t destination_port) noexcept
    {
      write_network_u16(transport, source_port);
      write_network_u16(transport + 2, destination_port);
      write_network_u16(transport + 4, static_cast<uint16_t>(transport_size));
      // Zero means "no checksum computed", which IPv4 permits for UDP.
      write_network_u16(transport + 6, 0);
    }

    struct FrameShape
    {
      MacAddress destination_mac;
      MacAddress source_mac;
      uint32_t source_host = 0;
      uint32_t destination_host = 0;
      bool udp = false;
      uint16_t source_port = 0;
      uint16_t destination_port = 0;
    };

    TrafficScenario frame_scenario(const TrafficGeneratorConfig& config, uint64_t sequence) noexcept
    {
      if (config.scenario != TrafficScenario::Mixed)
      {
        return config.scenario;
      }
      // Mixed predates the attack scenarios and stays a rotation of the three
      // benign ones; an attack belongs in a run that asked for it.
      return static_cast<TrafficScenario>(sequence % 3);
    }

    FrameShape
    frame_shape(const TrafficGeneratorConfig& config, TrafficScenario scenario, uint64_t sequence, uint64_t entropy) noexcept
    {
      FrameShape shape;
      switch (scenario)
      {
        case TrafficScenario::Mixed:
        case TrafficScenario::KnownUnicast:
        case TrafficScenario::Broadcast:
        case TrafficScenario::UnknownUnicast:
          shape.source_host = static_cast<uint32_t>(sequence % config.host_count);
          shape.destination_host = (shape.source_host + 1) % config.host_count;
          shape.source_mac = host_mac(shape.source_host);
          shape.destination_mac = host_mac(shape.destination_host);
          if (scenario == TrafficScenario::Broadcast)
          {
            shape.destination_mac = MacAddress::broadcast();
          }
          else if (scenario == TrafficScenario::UnknownUnicast)
          {
            shape.destination_mac = unknown_mac(entropy);
          }
          break;
        case TrafficScenario::UdpFlood:
          // One source hammering one service port is what the UDP rate rule,
          // which aggregates by source address, counts.
          shape.source_host = FLOOD_SOURCE_HOST;
          shape.destination_host = FLOOD_DESTINATION_HOST;
          shape.source_mac = host_mac(shape.source_host);
          shape.destination_mac = host_mac(shape.destination_host);
          shape.udp = true;
          shape.source_port = static_cast<uint16_t>(EPHEMERAL_PORT_BASE | (entropy & EPHEMERAL_PORT_MASK));
          shape.destination_port = UDP_FLOOD_DESTINATION_PORT;
          break;
        case TrafficScenario::PortScan:
          // Same source, same victim, marching destination port: the scan rule
          // counts distinct destination ports per source address.
          shape.source_host = FLOOD_SOURCE_HOST;
          shape.destination_host = FLOOD_DESTINATION_HOST;
          shape.source_mac = host_mac(shape.source_host);
          shape.destination_mac = host_mac(shape.destination_host);
          shape.udp = true;
          shape.source_port = PORT_SCAN_SOURCE_PORT;
          shape.destination_port = static_cast<uint16_t>(PORT_SCAN_FIRST_PORT + sequence % PORT_SCAN_PORT_SPAN);
          break;
        case TrafficScenario::BroadcastStorm:
          // A handful of hosts, every frame broadcast, so each source crosses
          // the per-source broadcast threshold rather than sharing it out.
          shape.source_host = static_cast<uint32_t>(sequence % std::min(config.host_count, BROADCAST_STORM_SOURCE_LIMIT));
          shape.destination_host = BROADCAST_STORM_DESTINATION_HOST;
          shape.source_mac = host_mac(shape.source_host);
          shape.destination_mac = MacAddress::broadcast();
          shape.udp = true;
          shape.source_port = BROADCAST_STORM_PORT;
          shape.destination_port = BROADCAST_STORM_PORT;
          break;
      }
      return shape;
    }
  }  // namespace

  size_t write_traffic_frame(const TrafficGeneratorConfig& config, uint64_t sequence, uint8_t* out) noexcept
  {
    if (out == nullptr || config.frame_size < ETHERNET_HEADER_SIZE || config.host_count < 2)
    {
      return 0;
    }

    uint64_t state = config.seed ^ (sequence * SPLITMIX_GAMMA);
    const uint64_t entropy = splitmix64(state);
    const FrameShape shape = frame_shape(config, frame_scenario(config, sequence), sequence, entropy);

    std::copy_n(shape.destination_mac.data(), MAC_ADDRESS_SIZE, out);
    std::copy_n(shape.source_mac.data(), MAC_ADDRESS_SIZE, out + MAC_ADDRESS_SIZE);
    write_network_u16(out + MAC_ADDRESS_SIZE * 2, EtherType::IPv4);

    uint8_t* const payload = out + ETHERNET_HEADER_SIZE;
    const size_t payload_size = config.frame_size - ETHERNET_HEADER_SIZE;
    fill_random_payload(state, payload, payload_size);

    if (payload_size < IPV4_HEADER_SIZE || payload_size > std::numeric_limits<uint16_t>::max())
    {
      return config.frame_size;
    }

    // A frame too small for the transport header keeps the largest header set
    // that fits rather than claiming a protocol whose header is not there.
    const bool with_udp = shape.udp && payload_size >= IPV4_HEADER_SIZE + UDP_HEADER_SIZE;
    write_ipv4_header(
        payload,
        payload_size,
        static_cast<uint16_t>(sequence),
        with_udp ? UDP_PROTOCOL : NO_TRANSPORT_PROTOCOL,
        shape.source_host,
        shape.destination_host);
    if (with_udp)
    {
      write_udp_header(
          payload + IPV4_HEADER_SIZE, payload_size - IPV4_HEADER_SIZE, shape.source_port, shape.destination_port);
    }
    return config.frame_size;
  }

  std::vector<uint8_t> traffic_frame(const TrafficGeneratorConfig& config, uint64_t sequence)
  {
    std::vector<uint8_t> frame(config.frame_size);
    frame.resize(write_traffic_frame(config, sequence, frame.data()));
    return frame;
  }

  DeterministicTrafficGenerator::DeterministicTrafficGenerator(TrafficGeneratorConfig config) : config_(config)
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

  std::vector<uint8_t> DeterministicTrafficGenerator::next_frame()
  {
    return traffic_frame(config_, sequence_++);
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
}  // namespace wirelab
