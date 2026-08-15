#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "wirelab/cuda_packet_parser.hpp"

namespace
{
  std::vector<uint8_t> make_ipv4_udp_frame()
  {
    std::vector<uint8_t> bytes(wirelab::ETHERNET_HEADER_SIZE + 20 + 8, 0);
    const std::array<uint8_t, wirelab::MAC_ADDRESS_SIZE> destination = { 0, 1, 2, 3, 4, 5 };
    const std::array<uint8_t, wirelab::MAC_ADDRESS_SIZE> source = { 6, 7, 8, 9, 10, 11 };
    for (size_t index = 0; index < wirelab::MAC_ADDRESS_SIZE; ++index)
    {
      bytes[index] = destination[index];
      bytes[wirelab::MAC_ADDRESS_SIZE + index] = source[index];
    }
    bytes[12] = 0x08;
    bytes[13] = 0x00;
    const size_t ip = wirelab::ETHERNET_HEADER_SIZE;
    bytes[ip] = 0x45;
    bytes[ip + 2] = 0;
    bytes[ip + 3] = 28;
    bytes[ip + 8] = 64;
    bytes[ip + 9] = 17;
    bytes[ip + 12] = 192;
    bytes[ip + 13] = 0;
    bytes[ip + 14] = 2;
    bytes[ip + 15] = 1;
    bytes[ip + 16] = 198;
    bytes[ip + 17] = 51;
    bytes[ip + 18] = 100;
    bytes[ip + 19] = 2;
    const size_t udp = ip + 20;
    bytes[udp] = 0x1f;
    bytes[udp + 1] = 0x90;
    bytes[udp + 2] = 0x00;
    bytes[udp + 3] = 0x35;
    bytes[udp + 5] = 8;
    return bytes;
  }

  TEST(CudaPacketParserTest, MatchesCpuParsingForValidAndMalformedFrames)
  {
    if (!wirelab::CudaPacketParser::is_available())
    {
      GTEST_SKIP() << "no compatible CUDA device is available";
    }

    const auto valid = make_ipv4_udp_frame();
    const std::array<uint8_t, wirelab::ETHERNET_HEADER_SIZE - 1> malformed{};
    const wirelab::PacketView views[] = {
      { valid.data(), valid.size(), 17 },
      { malformed.data(), malformed.size(), 18 },
      { nullptr, 0, 19 },
    };
    const auto batch = wirelab::PacketBatch::create(views, 3);

    wirelab::CpuPacketAnalyzer cpu;
    const auto cpu_result = cpu.analyze(*batch);
    wirelab::CudaPacketParser cuda;
    const auto cuda_parse_result = cuda.parse_with_timing(*batch);
    EXPECT_GT(cuda_parse_result.timing.host_to_device_ns + cuda_parse_result.timing.kernel_ns +
                  cuda_parse_result.timing.device_to_host_ns,
              0U);
    const auto& cuda_result = cuda_parse_result.packets;

    ASSERT_EQ(cuda_result.size(), cpu_result.packets.size());
    for (size_t index = 0; index < cuda_result.size(); ++index)
    {
      EXPECT_EQ(cuda_result[index].source_mac, cpu_result.packets[index].source_mac);
      EXPECT_EQ(cuda_result[index].destination_mac, cpu_result.packets[index].destination_mac);
      EXPECT_EQ(cuda_result[index].source_ipv4, cpu_result.packets[index].source_ipv4);
      EXPECT_EQ(cuda_result[index].destination_ipv4, cpu_result.packets[index].destination_ipv4);
      EXPECT_EQ(cuda_result[index].source_port, cpu_result.packets[index].source_port);
      EXPECT_EQ(cuda_result[index].destination_port, cpu_result.packets[index].destination_port);
      EXPECT_EQ(cuda_result[index].ethertype, cpu_result.packets[index].ethertype);
      EXPECT_EQ(cuda_result[index].frame_length, cpu_result.packets[index].frame_length);
      EXPECT_EQ(cuda_result[index].ingress_port, cpu_result.packets[index].ingress_port);
      EXPECT_EQ(cuda_result[index].protocol, cpu_result.packets[index].protocol);
      EXPECT_EQ(cuda_result[index].tcp_flags, cpu_result.packets[index].tcp_flags);
      EXPECT_EQ(cuda_result[index].flow_hash, cpu_result.packets[index].flow_hash);
      EXPECT_EQ(cuda_result[index].validity, cpu_result.packets[index].validity);
    }
  }

  TEST(CudaPacketAnalyzerTest, MatchesCpuAggregatesAndOrderedForwardingClassifications)
  {
    if (!wirelab::CudaPacketParser::is_available())
    {
      GTEST_SKIP() << "no compatible CUDA device is available";
    }

    const auto first = make_ipv4_udp_frame();
    auto second = first;
    second[0] = 6;
    second[1] = 7;
    second[2] = 8;
    second[3] = 9;
    second[4] = 10;
    second[5] = 11;
    const wirelab::PacketView views[] = {
      { first.data(), first.size(), 17 },
      { second.data(), second.size(), 18 },
      { first.data(), first.size(), 19 },
    };
    const auto batch = wirelab::PacketBatch::create(views, 3);
    ASSERT_TRUE(batch.has_value());

    wirelab::CpuPacketAnalyzer cpu;
    wirelab::CudaPacketAnalyzer cuda;
    const auto cpu_result = cpu.analyze(*batch);
    const auto cuda_result = cuda.analyze(*batch);

    EXPECT_EQ(cuda_result.received_packets, cpu_result.received_packets);
    EXPECT_EQ(cuda_result.received_bytes, cpu_result.received_bytes);
    EXPECT_EQ(cuda_result.malformed_packets, cpu_result.malformed_packets);
    EXPECT_EQ(cuda_result.broadcast_packets, cpu_result.broadcast_packets);
    EXPECT_EQ(cuda_result.unknown_unicast_packets, cpu_result.unknown_unicast_packets);
    EXPECT_EQ(cuda_result.known_unicast_packets, cpu_result.known_unicast_packets);
    for (size_t index = 0; index < cuda_result.frame_size_histogram.size(); ++index)
    {
      EXPECT_EQ(cuda_result.frame_size_histogram[index].inclusive_minimum,
                cpu_result.frame_size_histogram[index].inclusive_minimum);
      EXPECT_EQ(cuda_result.frame_size_histogram[index].inclusive_maximum,
                cpu_result.frame_size_histogram[index].inclusive_maximum);
      EXPECT_EQ(cuda_result.frame_size_histogram[index].packet_count,
                cpu_result.frame_size_histogram[index].packet_count);
      EXPECT_EQ(cuda_result.frame_size_histogram[index].byte_count, cpu_result.frame_size_histogram[index].byte_count);
    }
    ASSERT_EQ(cuda_result.ethertype_histogram.size(), cpu_result.ethertype_histogram.size());
    ASSERT_EQ(cuda_result.protocol_histogram.size(), cpu_result.protocol_histogram.size());
    ASSERT_EQ(cuda_result.destination_port_histogram.size(), cpu_result.destination_port_histogram.size());
    ASSERT_EQ(cuda_result.flows.size(), cpu_result.flows.size());
    ASSERT_EQ(cuda_result.source_mac_traffic.size(), cpu_result.source_mac_traffic.size());
    ASSERT_EQ(cuda_result.destination_mac_traffic.size(), cpu_result.destination_mac_traffic.size());
    ASSERT_EQ(cuda_result.mac_traffic_matrix.size(), cpu_result.mac_traffic_matrix.size());
    const auto expect_histogram_equal = [](const std::vector<wirelab::HistogramEntry>& actual,
                                           const std::vector<wirelab::HistogramEntry>& expected) {
      ASSERT_EQ(actual.size(), expected.size());
      for (size_t index = 0; index < actual.size(); ++index)
      {
        EXPECT_EQ(actual[index].value, expected[index].value);
        EXPECT_EQ(actual[index].packet_count, expected[index].packet_count);
        EXPECT_EQ(actual[index].byte_count, expected[index].byte_count);
      }
    };
    expect_histogram_equal(cuda_result.ethertype_histogram, cpu_result.ethertype_histogram);
    expect_histogram_equal(cuda_result.protocol_histogram, cpu_result.protocol_histogram);
    expect_histogram_equal(cuda_result.destination_port_histogram, cpu_result.destination_port_histogram);
    for (size_t index = 0; index < cuda_result.source_mac_traffic.size(); ++index)
    {
      EXPECT_EQ(cuda_result.source_mac_traffic[index].mac, cpu_result.source_mac_traffic[index].mac);
      EXPECT_EQ(cuda_result.source_mac_traffic[index].packet_count, cpu_result.source_mac_traffic[index].packet_count);
      EXPECT_EQ(cuda_result.source_mac_traffic[index].byte_count, cpu_result.source_mac_traffic[index].byte_count);
      EXPECT_EQ(cuda_result.destination_mac_traffic[index].mac, cpu_result.destination_mac_traffic[index].mac);
      EXPECT_EQ(cuda_result.destination_mac_traffic[index].packet_count,
                cpu_result.destination_mac_traffic[index].packet_count);
      EXPECT_EQ(cuda_result.destination_mac_traffic[index].byte_count, cpu_result.destination_mac_traffic[index].byte_count);
      EXPECT_EQ(cuda_result.mac_traffic_matrix[index].source_mac, cpu_result.mac_traffic_matrix[index].source_mac);
      EXPECT_EQ(cuda_result.mac_traffic_matrix[index].destination_mac,
                cpu_result.mac_traffic_matrix[index].destination_mac);
      EXPECT_EQ(cuda_result.mac_traffic_matrix[index].packet_count,
                cpu_result.mac_traffic_matrix[index].packet_count);
      EXPECT_EQ(cuda_result.mac_traffic_matrix[index].byte_count, cpu_result.mac_traffic_matrix[index].byte_count);
    }
    for (size_t index = 0; index < cuda_result.flows.size(); ++index)
    {
      EXPECT_EQ(cuda_result.flows[index].flow_hash, cpu_result.flows[index].flow_hash);
      EXPECT_EQ(cuda_result.flows[index].packet_count, cpu_result.flows[index].packet_count);
      EXPECT_EQ(cuda_result.flows[index].byte_count, cpu_result.flows[index].byte_count);
      EXPECT_EQ(cuda_result.flows[index].key.source_ipv4, cpu_result.flows[index].key.source_ipv4);
      EXPECT_EQ(cuda_result.flows[index].key.destination_ipv4, cpu_result.flows[index].key.destination_ipv4);
      EXPECT_EQ(cuda_result.flows[index].key.source_port, cpu_result.flows[index].key.source_port);
      EXPECT_EQ(cuda_result.flows[index].key.destination_port, cpu_result.flows[index].key.destination_port);
      EXPECT_EQ(cuda_result.flows[index].key.protocol, cpu_result.flows[index].key.protocol);
    }
    ASSERT_EQ(cuda_result.packets.size(), cpu_result.packets.size());
    for (size_t index = 0; index < cuda_result.packets.size(); ++index)
    {
      EXPECT_EQ(cuda_result.packets[index].classification, cpu_result.packets[index].classification);
      EXPECT_EQ(cuda_result.packets[index].validity, cpu_result.packets[index].validity);
      EXPECT_EQ(cuda_result.packets[index].flow_hash, cpu_result.packets[index].flow_hash);
    }
  }

  TEST(CudaPacketParserTest, PreservesPacketOrderAtKernelBoundaryBatchSizes)
  {
    if (!wirelab::CudaPacketParser::is_available())
    {
      GTEST_SKIP() << "no compatible CUDA device is available";
    }

    const auto frame = make_ipv4_udp_frame();
    const std::array<size_t, 12> batch_sizes = { 0, 1, 31, 32, 33, 127, 128, 129, 255, 256, 257, 1024 };
    wirelab::CudaPacketParser cuda;
    for (const size_t batch_size : batch_sizes)
    {
      std::vector<wirelab::PacketView> views;
      views.reserve(batch_size);
      for (size_t index = 0; index < batch_size; ++index)
      {
        views.push_back({ frame.data(), frame.size(), static_cast<uint32_t>(index) });
      }
      const auto batch = wirelab::PacketBatch::create(views.data(), views.size());
      ASSERT_TRUE(batch.has_value()) << "batch size " << batch_size;

      const auto result = cuda.parse(*batch);
      ASSERT_EQ(result.size(), batch_size) << "batch size " << batch_size;
      for (size_t index = 0; index < batch_size; ++index)
      {
        EXPECT_EQ(result[index].ingress_port, index) << "batch size " << batch_size;
        EXPECT_EQ(result[index].validity, wirelab::PacketValidity::Valid) << "batch size " << batch_size;
        EXPECT_EQ(result[index].source_port, 8080) << "batch size " << batch_size;
        EXPECT_EQ(result[index].destination_port, 53) << "batch size " << batch_size;
      }
    }
  }

  TEST(CudaPacketParserTest, RejectsMalformedContiguousBatchBeforeDeviceAccess)
  {
    wirelab::PacketBatch batch;
    batch.packet_offsets = { 0 };
    batch.packet_lengths = { 0 };

    wirelab::CudaPacketParser cuda;
    EXPECT_THROW(
        {
          const auto analyses = cuda.parse(batch);
          static_cast<void>(analyses);
        },
        std::invalid_argument);
  }
}
