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

    constexpr uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ULL;

    constexpr std::array<uint64_t, 7> FRAME_SIZE_BUCKET_MINIMUMS = {
      0,
      64,
      128,
      256,
      512,
      1024,
      1519,
    };
    constexpr uint64_t FNV1A_PRIME = 1099511628211ULL;

    void hash_byte(uint64_t& hash, uint8_t byte) noexcept
    {
      hash ^= byte;
      hash *= FNV1A_PRIME;
    }

    void hash_u16(uint64_t& hash, uint16_t value) noexcept
    {
      hash_byte(hash, static_cast<uint8_t>(value >> 8U));
      hash_byte(hash, static_cast<uint8_t>(value));
    }

    void hash_u32(uint64_t& hash, uint32_t value) noexcept
    {
      hash_byte(hash, static_cast<uint8_t>(value >> 24U));
      hash_byte(hash, static_cast<uint8_t>(value >> 16U));
      hash_byte(hash, static_cast<uint8_t>(value >> 8U));
      hash_byte(hash, static_cast<uint8_t>(value));
    }

    FlowKey make_flow_key(const PacketAnalysis& analysis) noexcept
    {
      return FlowKey{
        analysis.source_ipv4, analysis.destination_ipv4, analysis.source_port, analysis.destination_port, analysis.protocol
      };
    }

    uint64_t hash_flow_key(const FlowKey& key) noexcept
    {
      uint64_t hash = FNV1A_OFFSET_BASIS;
      hash_u32(hash, key.source_ipv4);
      hash_u32(hash, key.destination_ipv4);
      hash_u16(hash, key.source_port);
      hash_u16(hash, key.destination_port);
      hash_byte(hash, key.protocol);
      return hash;
    }

    bool flow_key_less(const FlowRecord& first, const FlowRecord& second) noexcept
    {
      if (first.key.source_ipv4 != second.key.source_ipv4)
      {
        return first.key.source_ipv4 < second.key.source_ipv4;
      }
      if (first.key.destination_ipv4 != second.key.destination_ipv4)
      {
        return first.key.destination_ipv4 < second.key.destination_ipv4;
      }
      if (first.key.source_port != second.key.source_port)
      {
        return first.key.source_port < second.key.source_port;
      }
      if (first.key.destination_port != second.key.destination_port)
      {
        return first.key.destination_port < second.key.destination_port;
      }
      return first.key.protocol < second.key.protocol;
    }

    void aggregate_histogram(std::vector<HistogramEntry>& histogram)
    {
      std::sort(
          histogram.begin(), histogram.end(),
          [](const HistogramEntry& first, const HistogramEntry& second) { return first.value < second.value; });
      size_t output_index = 0;
      for (size_t index = 0; index < histogram.size(); ++index)
      {
        const HistogramEntry& entry = histogram[index];
        if (output_index == 0 || histogram[output_index - 1].value != entry.value)
        {
          histogram[output_index] = entry;
          ++output_index;
          continue;
        }

        HistogramEntry& aggregate = histogram[output_index - 1];
        aggregate.packet_count += entry.packet_count;
        aggregate.byte_count += entry.byte_count;
      }
      histogram.resize(output_index);
    }

    void aggregate_mac_traffic(std::vector<MacTrafficRecord>& traffic)
    {
      std::sort(
          traffic.begin(), traffic.end(),
          [](const MacTrafficRecord& first, const MacTrafficRecord& second) { return first.mac < second.mac; });
      size_t output_index = 0;
      for (size_t index = 0; index < traffic.size(); ++index)
      {
        const MacTrafficRecord& entry = traffic[index];
        if (output_index == 0 || traffic[output_index - 1].mac != entry.mac)
        {
          traffic[output_index] = entry;
          ++output_index;
          continue;
        }

        MacTrafficRecord& aggregate = traffic[output_index - 1];
        aggregate.packet_count += entry.packet_count;
        aggregate.byte_count += entry.byte_count;
      }
      traffic.resize(output_index);
    }

    void rank_mac_traffic(std::vector<MacTrafficRecord>& traffic)
    {
      std::sort(
          traffic.begin(), traffic.end(), [](const MacTrafficRecord& first, const MacTrafficRecord& second) {
            if (first.byte_count != second.byte_count) return first.byte_count > second.byte_count;
            if (first.packet_count != second.packet_count) return first.packet_count > second.packet_count;
            return first.mac < second.mac;
          });
    }

    void rank_flows(std::vector<FlowRecord>& flows)
    {
      std::sort(flows.begin(), flows.end(), [](const FlowRecord& first, const FlowRecord& second) {
        if (first.byte_count != second.byte_count) return first.byte_count > second.byte_count;
        if (first.packet_count != second.packet_count) return first.packet_count > second.packet_count;
        return flow_key_less(first, second);
      });
    }

    void aggregate_traffic_matrix(std::vector<TrafficMatrixEntry>& matrix)
    {
      std::sort(
          matrix.begin(), matrix.end(), [](const TrafficMatrixEntry& first, const TrafficMatrixEntry& second) {
            return first.source_mac == second.source_mac ? first.destination_mac < second.destination_mac
                                                         : first.source_mac < second.source_mac;
          });
      size_t output_index = 0;
      for (size_t index = 0; index < matrix.size(); ++index)
      {
        const TrafficMatrixEntry& entry = matrix[index];
        if (output_index == 0 || matrix[output_index - 1].source_mac != entry.source_mac ||
            matrix[output_index - 1].destination_mac != entry.destination_mac)
        {
          matrix[output_index] = entry;
          ++output_index;
          continue;
        }

        TrafficMatrixEntry& aggregate = matrix[output_index - 1];
        aggregate.packet_count += entry.packet_count;
        aggregate.byte_count += entry.byte_count;
      }
      matrix.resize(output_index);
    }

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
    for (size_t index = 0; index < batch.frame_size_histogram.size(); ++index)
    {
      PacketSizeHistogramBucket& bucket = batch.frame_size_histogram[index];
      bucket.inclusive_minimum = FRAME_SIZE_BUCKET_MINIMUMS[index];
      bucket.inclusive_maximum = index + 1 == batch.frame_size_histogram.size()
                                     ? std::numeric_limits<uint64_t>::max()
                                     : FRAME_SIZE_BUCKET_MINIMUMS[index + 1] - 1;
    }
    if (packet_count == 0)
    {
      return batch;
    }

    batch.packets.reserve(packet_count);
    batch.ethertype_histogram.reserve(packet_count);
    batch.protocol_histogram.reserve(packet_count);
    batch.destination_port_histogram.reserve(packet_count);
    batch.flows.reserve(packet_count);
    batch.source_mac_traffic.reserve(packet_count);
    batch.destination_mac_traffic.reserve(packet_count);
    batch.mac_traffic_matrix.reserve(packet_count);
    for (size_t index = 0; index < packet_count; ++index)
    {
      const PacketView& packet = packets[index];
      PacketAnalysis analysis;
      analysis.frame_length = static_cast<uint16_t>(std::min(packet.size, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
      batch.received_packets += 1;
      batch.received_bytes += packet.size;
      const auto frame_size_bucket =
          static_cast<size_t>(std::upper_bound(
                                  FRAME_SIZE_BUCKET_MINIMUMS.begin() + 1,
                                  FRAME_SIZE_BUCKET_MINIMUMS.end(), packet.size) -
                              FRAME_SIZE_BUCKET_MINIMUMS.begin() - 1);
      PacketSizeHistogramBucket& size_bucket = batch.frame_size_histogram[frame_size_bucket];
      size_bucket.packet_count += 1;
      size_bucket.byte_count += packet.size;

      if (packet.bytes == nullptr || packet.size < ETHERNET_HEADER_SIZE)
      {
        batch.malformed_packets += 1;
        batch.packets.push_back(analysis);
        continue;
      }

      analysis.destination_mac = MacAddress(packet.bytes);
      analysis.source_mac = MacAddress(packet.bytes + MAC_ADDRESS_SIZE);
      analysis.ethertype = read_network_u16(packet.bytes + MAC_ADDRESS_SIZE * 2);
      batch.ethertype_histogram.push_back(HistogramEntry{ analysis.ethertype, 1, packet.size });
      batch.source_mac_traffic.push_back(MacTrafficRecord{ analysis.source_mac, 1, packet.size });
      batch.destination_mac_traffic.push_back(MacTrafficRecord{ analysis.destination_mac, 1, packet.size });
      batch.mac_traffic_matrix.push_back(
          TrafficMatrixEntry{ analysis.source_mac, analysis.destination_mac, 1, packet.size });
      analysis.validity =
          analysis.ethertype == EtherType::IPv4 ? parse_ipv4(packet, analysis) : PacketValidity::Valid;
      if (analysis.validity != PacketValidity::Valid)
      {
        batch.malformed_packets += 1;
      }
      if (analysis.ethertype == EtherType::IPv4 && analysis.validity == PacketValidity::Valid)
      {
        batch.protocol_histogram.push_back(HistogramEntry{ analysis.protocol, 1, packet.size });
        if (analysis.protocol == TCP_PROTOCOL || analysis.protocol == UDP_PROTOCOL)
        {
          batch.destination_port_histogram.push_back(
              HistogramEntry{ analysis.destination_port, 1, packet.size });
        }
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
      if (analysis.ethertype == EtherType::IPv4 && analysis.validity == PacketValidity::Valid)
      {
        const FlowKey key = make_flow_key(analysis);
        analysis.flow_hash = hash_flow_key(key);
        batch.flows.push_back(FlowRecord{ key, analysis.flow_hash, 1, packet.size });
      }
      batch.packets.push_back(analysis);
    }
    std::sort(batch.flows.begin(), batch.flows.end(), flow_key_less);
    size_t output_index = 0;
    for (size_t flow_index = 0; flow_index < batch.flows.size(); ++flow_index)
    {
      FlowRecord& flow = batch.flows[flow_index];
      if (output_index == 0 || !(batch.flows[output_index - 1].key == flow.key))
      {
        batch.flows[output_index] = flow;
        ++output_index;
        continue;
      }

      FlowRecord& aggregate = batch.flows[output_index - 1];
      aggregate.packet_count += flow.packet_count;
      aggregate.byte_count += flow.byte_count;
    }
    batch.flows.resize(output_index);
    aggregate_histogram(batch.ethertype_histogram);
    aggregate_histogram(batch.protocol_histogram);
    aggregate_histogram(batch.destination_port_histogram);
    aggregate_mac_traffic(batch.source_mac_traffic);
    aggregate_mac_traffic(batch.destination_mac_traffic);
    aggregate_traffic_matrix(batch.mac_traffic_matrix);
    rank_mac_traffic(batch.source_mac_traffic);
    rank_mac_traffic(batch.destination_mac_traffic);
    rank_flows(batch.flows);
    return batch;
  }

  void CpuPacketAnalyzer::reset() noexcept
  {
    learned_macs_.clear();
  }
}
