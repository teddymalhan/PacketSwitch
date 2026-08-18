// CPU-vs-Metal parity tests for the Apple GPU packet parser. The Metal
// analyzer must reproduce the CPU analyzer's AnalysisBatch exactly for the
// same packet sequence, because both feed the same ordered MAC-learning
// aggregator.

#include "wirelab/metal_packet_parser.hpp"

#include "wirelab/accelerated_backends.hpp"
#include "wirelab/benchmark.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
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

// The pipelined parser exists so a live caller does not block on the GPU. It is
// only worth having if it still agrees with the CPU analyzer batch for batch and
// in the order the batches were submitted, so that is what these pin down.

namespace
{
  wirelab::PacketBatch batch_of(const std::vector<std::vector<uint8_t>>& frames)
  {
    const auto views = to_views(frames);
    auto batch = wirelab::PacketBatch::create(views.data(), views.size());
    EXPECT_TRUE(batch.has_value());
    return *batch;
  }

  std::vector<std::vector<std::vector<uint8_t>>> learning_batches()
  {
    return {
      { valid_udp_frame(), broadcast_frame(), fragmented_frame() },
      { b_to_a_frame(), a_to_c_frame() },
      { valid_udp_frame() },
      { valid_tcp_frame(), valid_icmp_frame(), arp_frame(), short_frame() },
      { a_to_c_frame(), b_to_a_frame(), ipv6_frame() },
    };
  }
}

TEST(MetalStreamParserTest, RejectsAnUnusablePipelineDepth)
{
  EXPECT_THROW((void)wirelab::MetalStreamParser(0), std::invalid_argument);
  EXPECT_THROW((void)wirelab::MetalStreamParser(wirelab::MetalStreamParser::MAX_PIPELINE_DEPTH + 1),
               std::invalid_argument);
}

TEST(MetalStreamParserTest, KeepsBatchesInFlightInsteadOfBlockingOnEachOne)
{
  wirelab::MetalStreamParser parser(3);
  ASSERT_EQ(3U, parser.pipeline_depth());
  EXPECT_TRUE(parser.idle());

  for (size_t index = 0; index < parser.pipeline_depth(); ++index)
  {
    parser.submit(batch_of({ valid_udp_frame(), valid_tcp_frame() }));
  }
  // Nothing was collected, so every submitted batch is still outstanding: the
  // host never waited for a kernel it had not asked for.
  EXPECT_EQ(parser.pipeline_depth(), parser.in_flight());

  const auto batches = parser.drain();
  EXPECT_EQ(parser.pipeline_depth(), batches.size());
  EXPECT_TRUE(parser.idle());
}

TEST(MetalStreamParserTest, ReturnsBatchesInSubmissionOrder)
{
  wirelab::MetalStreamParser parser(2);
  const auto frames = learning_batches();
  for (const auto& batch : frames)
  {
    parser.submit(batch_of(batch));
  }

  const auto collected = parser.drain();
  ASSERT_EQ(frames.size(), collected.size());
  for (size_t index = 0; index < collected.size(); ++index)
  {
    EXPECT_EQ(index, collected[index].sequence);
    EXPECT_EQ(frames[index].size(), collected[index].packets.size());
  }
}

TEST(MetalStreamParserTest, AcceptsMoreBatchesThanTheRingHolds)
{
  // Depth one has no spare slot, so every submit past the first must reclaim the
  // oldest. The results of that reclaimed batch have to survive it.
  wirelab::MetalStreamParser parser(1);
  const auto frames = learning_batches();
  for (const auto& batch : frames)
  {
    parser.submit(batch_of(batch));
  }

  const auto collected = parser.drain();
  ASSERT_EQ(frames.size(), collected.size());
  for (size_t index = 0; index < collected.size(); ++index)
  {
    EXPECT_EQ(index, collected[index].sequence);
    EXPECT_EQ(frames[index].size(), collected[index].packets.size());
  }
}

TEST(MetalStreamParserTest, StopsAllocatingBuffersOnceTheWorkloadIsSteady)
{
  wirelab::MetalStreamParser parser(3);
  const std::vector<std::vector<uint8_t>> frames = { valid_udp_frame(), valid_tcp_frame(), broadcast_frame() };

  // Warm every slot, which is where the only legitimate allocations happen.
  for (size_t index = 0; index < 2 * parser.pipeline_depth(); ++index)
  {
    parser.submit(batch_of(frames));
    (void)parser.collect();
  }
  const size_t warmed = parser.buffer_allocations();
  EXPECT_GT(warmed, 0U);

  for (size_t index = 0; index < 20; ++index)
  {
    parser.submit(batch_of(frames));
    (void)parser.collect();
  }
  // A batch of the same shape must now reuse what the slots already hold.
  EXPECT_EQ(warmed, parser.buffer_allocations());
}

TEST(MetalStreamParserTest, GrowsBuffersForALargerBatchAndKeepsThemForTheNextOne)
{
  wirelab::MetalStreamParser parser(1);
  const std::vector<std::vector<uint8_t>> small = { valid_udp_frame() };
  std::vector<std::vector<uint8_t>> large;
  for (size_t index = 0; index < 64; ++index)
  {
    large.push_back(valid_udp_frame());
  }

  parser.submit(batch_of(small));
  (void)parser.collect();
  parser.submit(batch_of(large));
  (void)parser.collect();
  const size_t after_growth = parser.buffer_allocations();

  parser.submit(batch_of(large));
  (void)parser.collect();
  EXPECT_EQ(after_growth, parser.buffer_allocations());
}

TEST(MetalStreamParserTest, ReportsTransferInclusiveLatency)
{
  wirelab::MetalStreamParser parser(2);
  parser.submit(batch_of({ valid_udp_frame(), valid_tcp_frame(), broadcast_frame() }));
  const auto collected = parser.collect();
  ASSERT_TRUE(collected.has_value());

  const auto timing = collected->timing;
  EXPECT_GT(timing.host_to_device_ns, 0ULL);
  EXPECT_GT(timing.kernel_ns, 0ULL);
  // Submit to result in hand covers the fill and the read-back, so it cannot be
  // smaller than either of them.
  EXPECT_GE(timing.transfer_inclusive_ns, timing.host_to_device_ns);
  EXPECT_GE(timing.transfer_inclusive_ns, timing.device_to_host_ns);
}

TEST(MetalStreamParserTest, TryCollectDoesNotWaitForAnUnfinishedBatch)
{
  wirelab::MetalStreamParser parser(3);
  parser.submit(batch_of({ valid_udp_frame() }));
  // Whether this batch has finished yet is the GPU's business; either answer is
  // correct, and neither may lose the batch.
  const auto early = parser.try_collect();
  const size_t remaining = early.has_value() ? 0U : 1U;
  EXPECT_EQ(remaining, parser.in_flight());
  EXPECT_EQ(remaining, parser.drain().size());
}

TEST(MetalStreamParserTest, KeepsAnEmptyBatchInSequence)
{
  wirelab::MetalStreamParser parser(2);
  parser.submit(batch_of({ valid_udp_frame() }));
  parser.submit(batch_of({}));
  parser.submit(batch_of({ valid_tcp_frame() }));

  const auto collected = parser.drain();
  ASSERT_EQ(3U, collected.size());
  EXPECT_EQ(0U, collected[0].sequence);
  EXPECT_EQ(1U, collected[1].sequence);
  EXPECT_EQ(2U, collected[2].sequence);
  EXPECT_EQ(1U, collected[0].packets.size());
  EXPECT_TRUE(collected[1].packets.empty());
  EXPECT_EQ(1U, collected[2].packets.size());
}

TEST(MetalStreamingAnalyzerTest, MatchesCpuAcrossLearningBatchesWhilePipelined)
{
  wirelab::CpuPacketAnalyzer cpu;
  wirelab::MetalStreamingAnalyzer metal(3);
  const auto frames = learning_batches();

  std::vector<wirelab::AnalysisBatch> expected;
  for (const auto& batch : frames)
  {
    expected.push_back(cpu.analyze(batch_of(batch)));
  }

  // Submit everything before collecting anything: the ordered MAC learning has
  // to survive the host running ahead of the GPU.
  for (const auto& batch : frames)
  {
    metal.submit(batch_of(batch));
  }
  const auto actual = metal.drain();

  ASSERT_EQ(expected.size(), actual.size());
  for (size_t index = 0; index < expected.size(); ++index)
  {
    expect_same_analysis(expected[index], actual[index]);
  }
}

TEST(MetalStreamingAnalyzerTest, MatchesTheSynchronousMetalAnalyzer)
{
  wirelab::MetalPacketAnalyzer synchronous;
  wirelab::MetalStreamingAnalyzer streaming(2);
  for (const auto& frames : learning_batches())
  {
    const auto batch = batch_of(frames);
    const auto expected = synchronous.analyze(batch);
    const auto actual = streaming.analyze(batch);
    expect_same_analysis(expected, actual);
  }
}

TEST(MetalStreamingAnalyzerTest, DrainsOutstandingWorkBeforeAnsweringSynchronously)
{
  wirelab::CpuPacketAnalyzer cpu;
  wirelab::MetalStreamingAnalyzer metal(3);
  const auto first = batch_of({ valid_udp_frame(), broadcast_frame() });
  const auto second = batch_of({ b_to_a_frame(), a_to_c_frame() });

  const auto expected_first = cpu.analyze(first);
  const auto expected_second = cpu.analyze(second);

  metal.submit(first);
  // Mixing the shapes must not reorder learning: the submitted batch precedes
  // this one, so it has to be folded in first.
  const auto actual_second = metal.analyze(second);
  EXPECT_EQ(0U, metal.in_flight());
  expect_same_analysis(expected_second, actual_second);
  (void)expected_first;
}

TEST(MetalStreamingAnalyzerTest, ResetForgetsLearnedMacs)
{
  wirelab::MetalStreamingAnalyzer metal(2);
  const auto frames = batch_of({ valid_udp_frame(), b_to_a_frame() });
  const auto first = metal.analyze(frames);
  metal.reset();
  const auto second = metal.analyze(frames);
  expect_same_analysis(first, second);
}

// A faster backend that counts differently is not a faster backend, so the
// pipelined path has to reproduce the other two end to end, however it is driven.

namespace
{
  using BenchmarkCounters =
      std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>;

  BenchmarkCounters counters_of(const wirelab::BenchmarkResult& result)
  {
    return { result.total_packets,           result.completed_packets, result.received_packets,
             result.received_bytes,          result.malformed_packets, result.broadcast_packets,
             result.unknown_unicast_packets, result.known_unicast_packets };
  }

  wirelab::BenchmarkConfig benchmark_config(const std::string& backend)
  {
    wirelab::BenchmarkConfig config;
    config.traffic.scenario = wirelab::TrafficScenario::Mixed;
    config.traffic.seed = 42;
    config.traffic.frame_size = 128;
    config.packet_count = 512;
    config.batch_size = 32;
    config.backend = backend;
    return config;
  }

  wirelab::BenchmarkResult run_benchmark(const std::string& backend, size_t slice)
  {
    auto run = wirelab::BenchmarkRun::create(benchmark_config(backend),
                                             wirelab::accelerated_benchmark_backend_factory(),
                                             wirelab::accelerated_traffic_source_factory());
    EXPECT_TRUE(run.has_value());
    while (!run->finished())
    {
      run->advance(slice);
    }
    return run->result();
  }
}

TEST(MetalLiveBenchmarkTest, CountsWhatTheCpuAndSynchronousMetalBackendsCount)
{
  const auto cpu = run_benchmark("cpu", 512);
  const auto metal = run_benchmark("metal", 512);
  const auto live = run_benchmark("metal-live", 512);
  EXPECT_EQ(counters_of(cpu), counters_of(metal));
  EXPECT_EQ(counters_of(cpu), counters_of(live));
}

TEST(MetalLiveBenchmarkTest, CountsTheSameWhenDrivenInSlices)
{
  // Slicing must not change the counters, and a pipelined run has outstanding
  // work at a slice boundary, which is exactly where that could break.
  const auto whole = run_benchmark("metal-live", 512);
  const auto sliced = run_benchmark("metal-live", 32);
  const auto tiny = run_benchmark("metal-live", 1);
  EXPECT_EQ(counters_of(whole), counters_of(sliced));
  EXPECT_EQ(counters_of(whole), counters_of(tiny));
}

TEST(MetalLiveBenchmarkTest, ReportsTransferInclusiveLatencyThatTheOthersCannot)
{
  const auto live = run_benchmark("metal-live", 512);
  const auto metal = run_benchmark("metal", 512);
  EXPECT_GT(live.timing.transfer_inclusive_ns, 0ULL);
  EXPECT_GT(live.timing.kernel_ns, 0ULL);
  // The batch-at-a-time backend blocks, so its batch latency already is the
  // transfer-inclusive number and it reports none separately.
  EXPECT_EQ(0ULL, metal.timing.transfer_inclusive_ns);
  EXPECT_GT(live.batch_analysis_latency_p50_ns, 0ULL);
}

TEST(MetalLiveBenchmarkTest, IsCompiledInUnderItsOwnName)
{
  EXPECT_TRUE(wirelab::benchmark_backend_is_compiled_in("metal-live"));
}
