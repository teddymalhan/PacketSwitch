// CPU-vs-Metal parity tests for the Apple GPU packet parser. The Metal
// analyzer must reproduce the CPU analyzer's AnalysisBatch exactly for the
// same packet sequence, because both feed the same ordered MAC-learning
// aggregator.

#include "wirelab/metal_packet_parser.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
  constexpr std::array<uint8_t, 6> MAC_A{ 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
  constexpr std::array<uint8_t, 6> MAC_B{ 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
  constexpr std::array<uint8_t, 6> MAC_C{ 0x02, 0x00, 0x00, 0x00, 0x00, 0x03 };
  constexpr std::array<uint8_t, 6> MAC_BROADCAST{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

  void push_u16(std::vector<uint8_t>& bytes, uint16_t value)
  {
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value));
  }

  void push_u32(std::vector<uint8_t>& bytes, uint32_t value)
  {
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value));
  }

  std::vector<uint8_t> ethernet(const std::array<uint8_t, 6>& destination, const std::array<uint8_t, 6>& source,
                                uint16_t ethertype, std::vector<uint8_t> payload)
  {
    std::vector<uint8_t> frame;
    frame.insert(frame.end(), destination.begin(), destination.end());
    frame.insert(frame.end(), source.begin(), source.end());
    push_u16(frame, ethertype);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
  }

  std::vector<uint8_t> ipv4(uint8_t version_ihl, uint16_t total_length, uint16_t fragment_field, uint8_t protocol,
                            uint32_t source, uint32_t destination, std::vector<uint8_t> transport)
  {
    std::vector<uint8_t> header;
    header.push_back(version_ihl);
    header.push_back(0);
    push_u16(header, total_length);
    push_u16(header, 0x1234);
    push_u16(header, fragment_field);
    header.push_back(64);
    header.push_back(protocol);
    push_u16(header, 0);
    push_u32(header, source);
    push_u32(header, destination);
    header.insert(header.end(), transport.begin(), transport.end());
    return header;
  }

  std::vector<uint8_t> udp(uint16_t source_port, uint16_t destination_port, size_t payload_size)
  {
    std::vector<uint8_t> datagram;
    push_u16(datagram, source_port);
    push_u16(datagram, destination_port);
    push_u16(datagram, static_cast<uint16_t>(8 + payload_size));
    push_u16(datagram, 0);
    datagram.resize(datagram.size() + payload_size, 0xAB);
    return datagram;
  }

  std::vector<uint8_t> tcp(uint16_t source_port, uint16_t destination_port, uint8_t data_offset_flags)
  {
    std::vector<uint8_t> segment(20, 0);
    segment[0] = static_cast<uint8_t>(source_port >> 8U);
    segment[1] = static_cast<uint8_t>(source_port);
    segment[2] = static_cast<uint8_t>(destination_port >> 8U);
    segment[3] = static_cast<uint8_t>(destination_port);
    segment[12] = data_offset_flags;
    segment[13] = 0x10;
    return segment;
  }

  std::vector<uint8_t> valid_udp_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 32, 0, 17, 0x0A000001, 0x0A000002, udp(1234, 80, 4)));
  }

  std::vector<uint8_t> valid_tcp_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 40, 0, 6, 0x0A000001, 0x0A000002, tcp(2222, 443, 0x50)));
  }

  std::vector<uint8_t> valid_icmp_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 24, 0, 1, 0x0A000001, 0x0A000002, { 8, 0, 0, 0 }));
  }

  std::vector<uint8_t> arp_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0806, { 0, 1, 8, 0, 6, 4, 0, 1 });
  }

  std::vector<uint8_t> broadcast_frame()
  {
    return ethernet(MAC_BROADCAST, MAC_A, 0x0800,
                    ipv4(0x45, 32, 0, 17, 0x0A000001, 0xFFFFFFFF, udp(53, 5353, 4)));
  }

  std::vector<uint8_t> fragmented_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 32, 0x0001, 17, 0x0A000001, 0x0A000002, udp(9999, 9998, 4)));
  }

  std::vector<uint8_t> short_frame()
  {
    return { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 };
  }

  std::vector<uint8_t> ipv4_short_payload_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800,
                    { 0x45, 0, 0, 0, 0, 0, 0, 0, 64, 17, 0, 0, 10, 0, 0, 1, 10, 0, 0, 2 });
  }

  std::vector<uint8_t> ipv4_wrong_version_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x65, 32, 0, 17, 0x0A000001, 0x0A000002, udp(1, 2, 4)));
  }

  std::vector<uint8_t> ipv4_total_too_large_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 100, 0, 17, 0x0A000001, 0x0A000002, udp(1, 2, 4)));
  }

  std::vector<uint8_t> udp_len_too_short_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800,
                    ipv4(0x45, 24, 0, 17, 0x0A000001, 0x0A000002, { 1, 2, 0, 7, 0, 0, 0, 0 }));
  }

  std::vector<uint8_t> udp_len_too_large_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800,
                    ipv4(0x45, 24, 0, 17, 0x0A000001, 0x0A000002, { 1, 2, 0, 20, 0, 0, 0, 0 }));
  }

  std::vector<uint8_t> tcp_too_short_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800,
                    ipv4(0x45, 30, 0, 6, 0x0A000001, 0x0A000002, std::vector<uint8_t>(10, 0)));
  }

  std::vector<uint8_t> tcp_data_offset_too_large_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 40, 0, 6, 0x0A000001, 0x0A000002, tcp(5, 6, 0x60)));
  }

  std::vector<uint8_t> icmp_too_short_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x0800, ipv4(0x45, 22, 0, 1, 0x0A000001, 0x0A000002, { 8, 0 }));
  }

  std::vector<uint8_t> ipv6_frame()
  {
    return ethernet(MAC_B, MAC_A, 0x86DD, std::vector<uint8_t>(40, 0));
  }

  std::vector<uint8_t> b_to_a_frame()
  {
    return ethernet(MAC_A, MAC_B, 0x0800, ipv4(0x45, 32, 0, 17, 0x0A000002, 0x0A000001, udp(80, 1234, 4)));
  }

  std::vector<uint8_t> a_to_c_frame()
  {
    return ethernet(MAC_C, MAC_A, 0x0800, ipv4(0x45, 32, 0, 17, 0x0A000001, 0x0A000003, udp(7, 9, 4)));
  }

  void expect_same_histogram(const std::vector<wirelab::HistogramEntry>& expected,
                             const std::vector<wirelab::HistogramEntry>& actual)
  {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
      EXPECT_EQ(expected[index].value, actual[index].value);
      EXPECT_EQ(expected[index].packet_count, actual[index].packet_count);
      EXPECT_EQ(expected[index].byte_count, actual[index].byte_count);
    }
  }

  void expect_same_mac_traffic(const std::vector<wirelab::MacTrafficRecord>& expected,
                               const std::vector<wirelab::MacTrafficRecord>& actual)
  {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
      EXPECT_EQ(expected[index].mac, actual[index].mac);
      EXPECT_EQ(expected[index].packet_count, actual[index].packet_count);
      EXPECT_EQ(expected[index].byte_count, actual[index].byte_count);
    }
  }

  void expect_same_matrix(const std::vector<wirelab::TrafficMatrixEntry>& expected,
                          const std::vector<wirelab::TrafficMatrixEntry>& actual)
  {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
      EXPECT_EQ(expected[index].source_mac, actual[index].source_mac);
      EXPECT_EQ(expected[index].destination_mac, actual[index].destination_mac);
      EXPECT_EQ(expected[index].packet_count, actual[index].packet_count);
      EXPECT_EQ(expected[index].byte_count, actual[index].byte_count);
    }
  }

  void expect_same_flows(const std::vector<wirelab::FlowRecord>& expected, const std::vector<wirelab::FlowRecord>& actual)
  {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
      EXPECT_EQ(expected[index].key.source_ipv4, actual[index].key.source_ipv4);
      EXPECT_EQ(expected[index].key.destination_ipv4, actual[index].key.destination_ipv4);
      EXPECT_EQ(expected[index].key.source_port, actual[index].key.source_port);
      EXPECT_EQ(expected[index].key.destination_port, actual[index].key.destination_port);
      EXPECT_EQ(expected[index].key.protocol, actual[index].key.protocol);
      EXPECT_EQ(expected[index].flow_hash, actual[index].flow_hash);
      EXPECT_EQ(expected[index].packet_count, actual[index].packet_count);
      EXPECT_EQ(expected[index].byte_count, actual[index].byte_count);
    }
  }

  void expect_same_packets(const std::vector<wirelab::PacketAnalysis>& expected,
                           const std::vector<wirelab::PacketAnalysis>& actual)
  {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
      EXPECT_EQ(expected[index].source_mac, actual[index].source_mac);
      EXPECT_EQ(expected[index].destination_mac, actual[index].destination_mac);
      EXPECT_EQ(expected[index].source_ipv4, actual[index].source_ipv4);
      EXPECT_EQ(expected[index].destination_ipv4, actual[index].destination_ipv4);
      EXPECT_EQ(expected[index].source_port, actual[index].source_port);
      EXPECT_EQ(expected[index].destination_port, actual[index].destination_port);
      EXPECT_EQ(expected[index].ethertype, actual[index].ethertype);
      EXPECT_EQ(expected[index].frame_length, actual[index].frame_length);
      EXPECT_EQ(expected[index].ingress_port, actual[index].ingress_port);
      EXPECT_EQ(expected[index].protocol, actual[index].protocol);
      EXPECT_EQ(expected[index].tcp_flags, actual[index].tcp_flags);
      EXPECT_EQ(expected[index].flow_hash, actual[index].flow_hash);
      EXPECT_EQ(expected[index].validity, actual[index].validity);
      EXPECT_EQ(expected[index].classification, actual[index].classification);
    }
  }

  void expect_same_analysis(const wirelab::AnalysisBatch& expected, const wirelab::AnalysisBatch& actual)
  {
    EXPECT_EQ(expected.received_packets, actual.received_packets);
    EXPECT_EQ(expected.received_bytes, actual.received_bytes);
    EXPECT_EQ(expected.malformed_packets, actual.malformed_packets);
    EXPECT_EQ(expected.broadcast_packets, actual.broadcast_packets);
    EXPECT_EQ(expected.unknown_unicast_packets, actual.unknown_unicast_packets);
    EXPECT_EQ(expected.known_unicast_packets, actual.known_unicast_packets);

    ASSERT_EQ(expected.frame_size_histogram.size(), actual.frame_size_histogram.size());
    for (size_t index = 0; index < expected.frame_size_histogram.size(); ++index)
    {
      EXPECT_EQ(expected.frame_size_histogram[index].inclusive_minimum,
                actual.frame_size_histogram[index].inclusive_minimum);
      EXPECT_EQ(expected.frame_size_histogram[index].inclusive_maximum,
                actual.frame_size_histogram[index].inclusive_maximum);
      EXPECT_EQ(expected.frame_size_histogram[index].packet_count, actual.frame_size_histogram[index].packet_count);
      EXPECT_EQ(expected.frame_size_histogram[index].byte_count, actual.frame_size_histogram[index].byte_count);
    }

    expect_same_histogram(expected.ethertype_histogram, actual.ethertype_histogram);
    expect_same_histogram(expected.protocol_histogram, actual.protocol_histogram);
    expect_same_histogram(expected.destination_port_histogram, actual.destination_port_histogram);
    expect_same_mac_traffic(expected.source_mac_traffic, actual.source_mac_traffic);
    expect_same_mac_traffic(expected.destination_mac_traffic, actual.destination_mac_traffic);
    expect_same_matrix(expected.mac_traffic_matrix, actual.mac_traffic_matrix);
    expect_same_flows(expected.flows, actual.flows);
    expect_same_packets(expected.packets, actual.packets);
  }

  std::vector<wirelab::PacketView> to_views(const std::vector<std::vector<uint8_t>>& frames)
  {
    std::vector<wirelab::PacketView> views;
    views.reserve(frames.size());
    uint32_t port = 0;
    for (const auto& frame : frames)
    {
      views.push_back(wirelab::PacketView{ frame.data(), frame.size(), ++port });
    }
    return views;
  }
}

TEST(MetalPacketParserTest, IsAvailableOnMetalCapableMac)
{
  EXPECT_TRUE(wirelab::MetalPacketParser::is_available());
}

TEST(MetalPacketParserTest, MatchesCpuOnMixedBatch)
{
  const std::vector<std::vector<uint8_t>> frames = {
    valid_udp_frame(),   valid_tcp_frame(),       valid_icmp_frame(),      arp_frame(),
    broadcast_frame(),   fragmented_frame(),      short_frame(),           ipv4_short_payload_frame(),
    ipv4_wrong_version_frame(), ipv4_total_too_large_frame(), udp_len_too_short_frame(),
    udp_len_too_large_frame(),   tcp_too_short_frame(),      tcp_data_offset_too_large_frame(),
    icmp_too_short_frame(),      ipv6_frame(),
  };
  const auto views = to_views(frames);
  const auto batch = wirelab::PacketBatch::create(views.data(), views.size());
  ASSERT_TRUE(batch.has_value());

  wirelab::CpuPacketAnalyzer cpu;
  wirelab::MetalPacketAnalyzer metal;
  const auto cpu_result = cpu.analyze(*batch);
  const auto metal_result = metal.analyze(*batch);
  expect_same_analysis(cpu_result, metal_result);
}

TEST(MetalPacketParserTest, MatchesCpuOnPacketViewPath)
{
  const std::vector<std::vector<uint8_t>> frames = {
    valid_udp_frame(), broadcast_frame(), fragmented_frame(), short_frame(), ipv6_frame(),
  };
  const auto views = to_views(frames);

  wirelab::CpuPacketAnalyzer cpu;
  wirelab::MetalPacketAnalyzer metal;
  const auto cpu_result = cpu.analyze(views.data(), views.size());
  const auto metal_result = metal.analyze(views.data(), views.size());
  expect_same_analysis(cpu_result, metal_result);
}

TEST(MetalPacketParserTest, MatchesCpuAcrossLearningBatches)
{
  const std::vector<std::vector<uint8_t>> batch_one = { valid_udp_frame(), broadcast_frame(), fragmented_frame() };
  const std::vector<std::vector<uint8_t>> batch_two = { b_to_a_frame(), a_to_c_frame() };
  const std::vector<std::vector<uint8_t>> batch_three = { valid_udp_frame() };

  wirelab::CpuPacketAnalyzer cpu;
  wirelab::MetalPacketAnalyzer metal;
  for (const auto& frames : { batch_one, batch_two, batch_three })
  {
    const auto views = to_views(frames);
    const auto batch = wirelab::PacketBatch::create(views.data(), views.size());
    ASSERT_TRUE(batch.has_value());
    const auto cpu_result = cpu.analyze(*batch);
    const auto metal_result = metal.analyze(*batch);
    expect_same_analysis(cpu_result, metal_result);
  }
}

TEST(MetalPacketParserTest, ReportsKernelTiming)
{
  const auto views = to_views({ valid_udp_frame(), valid_tcp_frame(), broadcast_frame() });
  const auto batch = wirelab::PacketBatch::create(views.data(), views.size());
  ASSERT_TRUE(batch.has_value());

  wirelab::MetalPacketAnalyzer metal;
  const auto analysis = metal.analyze(*batch);
  EXPECT_GT(analysis.received_packets, 0ULL);
  const auto timing = metal.last_timing();
  EXPECT_GT(timing.kernel_ns, 0ULL);
  EXPECT_GT(timing.host_to_device_ns, 0ULL);
}
