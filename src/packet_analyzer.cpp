#include "project/packet_analyzer.hpp"

#include <algorithm>
#include <limits>

namespace project
{
  namespace
  {
    uint16_t read_network_u16(const uint8_t* bytes) noexcept
    {
      return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8U | bytes[1]);
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
