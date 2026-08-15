#include "project/packet_analyzer.hpp"

#include <algorithm>
#include <limits>

namespace project
{
  namespace
  {
    constexpr uint8_t IPV4_VERSION = 4;
    constexpr uint8_t TCP_PROTOCOL = 6;
    constexpr uint8_t UDP_PROTOCOL = 17;
    constexpr uint8_t ICMP_PROTOCOL = 1;
    constexpr size_t IPV4_MINIMUM_HEADER_SIZE = 20;
    constexpr size_t UDP_HEADER_SIZE = 8;
    constexpr size_t TCP_MINIMUM_HEADER_SIZE = 20;
    constexpr size_t ICMP_MINIMUM_HEADER_SIZE = 4;

    uint16_t read_network_u16(const uint8_t* bytes) noexcept
    {
      return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8U | bytes[1]);
    }

    uint32_t read_network_u32(const uint8_t* bytes) noexcept
    {
      return static_cast<uint32_t>(static_cast<uint32_t>(bytes[0]) << 24U |
                                   static_cast<uint32_t>(bytes[1]) << 16U |
                                   static_cast<uint32_t>(bytes[2]) << 8U | bytes[3]);
    }

    PacketValidity parse_ipv4(const PacketView& packet, PacketAnalysis& analysis) noexcept
    {
      const size_t payload_size = packet.size - ETHERNET_HEADER_SIZE;
      if (payload_size < IPV4_MINIMUM_HEADER_SIZE)
      {
        return PacketValidity::MalformedIpv4;
      }

      const uint8_t* const ipv4 = packet.bytes + ETHERNET_HEADER_SIZE;
      const uint8_t version = static_cast<uint8_t>(ipv4[0] >> 4U);
      const size_t header_size = static_cast<size_t>(ipv4[0] & 0x0fU) * 4U;
      const size_t total_size = read_network_u16(ipv4 + 2);
      if (version != IPV4_VERSION || header_size < IPV4_MINIMUM_HEADER_SIZE || header_size > payload_size ||
          total_size < header_size || total_size > payload_size)
      {
        return PacketValidity::MalformedIpv4;
      }

      analysis.source_ipv4 = read_network_u32(ipv4 + 12);
      analysis.destination_ipv4 = read_network_u32(ipv4 + 16);
      analysis.protocol = ipv4[9];
      if ((read_network_u16(ipv4 + 6) & 0x1fffU) != 0)
      {
        return PacketValidity::Valid;
      }

      const size_t transport_size = total_size - header_size;
      const uint8_t* const transport = ipv4 + header_size;
      if (analysis.protocol == UDP_PROTOCOL)
      {
        if (transport_size < UDP_HEADER_SIZE || read_network_u16(transport + 4) < UDP_HEADER_SIZE ||
            read_network_u16(transport + 4) > transport_size)
        {
          return PacketValidity::MalformedTransport;
        }
        analysis.source_port = read_network_u16(transport);
        analysis.destination_port = read_network_u16(transport + 2);
      }
      else if (analysis.protocol == TCP_PROTOCOL)
      {
        if (transport_size < TCP_MINIMUM_HEADER_SIZE)
        {
          return PacketValidity::MalformedTransport;
        }
        const size_t tcp_header_size = static_cast<size_t>(transport[12] >> 4U) * 4U;
        if (tcp_header_size < TCP_MINIMUM_HEADER_SIZE || tcp_header_size > transport_size)
        {
          return PacketValidity::MalformedTransport;
        }
        analysis.source_port = read_network_u16(transport);
        analysis.destination_port = read_network_u16(transport + 2);
        analysis.tcp_flags = transport[13];
      }
      else if (analysis.protocol == ICMP_PROTOCOL && transport_size < ICMP_MINIMUM_HEADER_SIZE)
      {
        return PacketValidity::MalformedTransport;
      }
      return PacketValidity::Valid;
    }
  }

  AnalysisBatch CpuPacketAnalyzer::analyze(const PacketView* packets, size_t packet_count)
  {
    AnalysisBatch batch;
    if (packet_count == 0)
    {
      return batch;
    }

    batch.packets.reserve(packet_count);
    for (size_t index = 0; index < packet_count; ++index)
    {
      const PacketView& packet = packets[index];
      PacketAnalysis analysis;
      analysis.frame_length = static_cast<uint16_t>(std::min(packet.size, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
      batch.received_packets += 1;
      batch.received_bytes += packet.size;

      if (packet.bytes == nullptr || packet.size < ETHERNET_HEADER_SIZE)
      {
        batch.malformed_packets += 1;
        batch.packets.push_back(analysis);
        continue;
      }

      analysis.destination_mac = MacAddress(packet.bytes);
      analysis.source_mac = MacAddress(packet.bytes + MAC_ADDRESS_SIZE);
      analysis.ethertype = read_network_u16(packet.bytes + MAC_ADDRESS_SIZE * 2);
      analysis.validity =
          analysis.ethertype == EtherType::IPv4 ? parse_ipv4(packet, analysis) : PacketValidity::Valid;
      if (analysis.validity != PacketValidity::Valid)
      {
        batch.malformed_packets += 1;
      }

      if (analysis.destination_mac.is_broadcast())
      {
        analysis.classification = PacketClassification::Broadcast;
        batch.broadcast_packets += 1;
      }
      else if (learned_macs_.find(analysis.destination_mac) == learned_macs_.end())
      {
        analysis.classification = PacketClassification::UnknownUnicast;
        batch.unknown_unicast_packets += 1;
      }
      else
      {
        analysis.classification = PacketClassification::KnownUnicast;
        batch.known_unicast_packets += 1;
      }
      learned_macs_.insert(analysis.source_mac);
      batch.packets.push_back(analysis);
    }
    return batch;
  }

  void CpuPacketAnalyzer::reset() noexcept
  {
    learned_macs_.clear();
  }
}
