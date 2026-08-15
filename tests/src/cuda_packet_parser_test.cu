#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "project/cuda_packet_parser.hpp"

namespace
{
  std::vector<uint8_t> make_ipv4_udp_frame()
  {
    std::vector<uint8_t> bytes(project::ETHERNET_HEADER_SIZE + 20 + 8, 0);
    const std::array<uint8_t, project::MAC_ADDRESS_SIZE> destination = { 0, 1, 2, 3, 4, 5 };
    const std::array<uint8_t, project::MAC_ADDRESS_SIZE> source = { 6, 7, 8, 9, 10, 11 };
    for (size_t index = 0; index < project::MAC_ADDRESS_SIZE; ++index)
    {
      bytes[index] = destination[index];
      bytes[project::MAC_ADDRESS_SIZE + index] = source[index];
    }
    bytes[12] = 0x08;
    bytes[13] = 0x00;
    const size_t ip = project::ETHERNET_HEADER_SIZE;
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
    if (!project::CudaPacketParser::is_available())
    {
      GTEST_SKIP() << "no compatible CUDA device is available";
    }

    const auto valid = make_ipv4_udp_frame();
    const std::array<uint8_t, project::ETHERNET_HEADER_SIZE - 1> malformed{};
    const project::PacketView views[] = {
      { valid.data(), valid.size(), 17 },
      { malformed.data(), malformed.size(), 18 },
      { nullptr, 0, 19 },
    };
    const auto batch = project::PacketBatch::create(views, 3);

    project::CpuPacketAnalyzer cpu;
    project::CudaPacketParser cuda;
    const auto cpu_result = cpu.analyze(*batch);
    const auto cuda_result = cuda.parse(*batch);

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

  TEST(CudaPacketParserTest, PreservesPacketOrderAtKernelBoundaryBatchSizes)
  {
    if (!project::CudaPacketParser::is_available())
    {
      GTEST_SKIP() << "no compatible CUDA device is available";
    }

    const auto frame = make_ipv4_udp_frame();
    const std::array<size_t, 12> batch_sizes = { 0, 1, 31, 32, 33, 127, 128, 129, 255, 256, 257, 1024 };
    project::CudaPacketParser cuda;
    for (const size_t batch_size : batch_sizes)
    {
      std::vector<project::PacketView> views;
      views.reserve(batch_size);
      for (size_t index = 0; index < batch_size; ++index)
      {
        views.push_back({ frame.data(), frame.size(), static_cast<uint32_t>(index) });
      }
      const auto batch = project::PacketBatch::create(views.data(), views.size());
      ASSERT_TRUE(batch.has_value()) << "batch size " << batch_size;

      const auto result = cuda.parse(*batch);
      ASSERT_EQ(result.size(), batch_size) << "batch size " << batch_size;
      for (size_t index = 0; index < batch_size; ++index)
      {
        EXPECT_EQ(result[index].ingress_port, index) << "batch size " << batch_size;
        EXPECT_EQ(result[index].validity, project::PacketValidity::Valid) << "batch size " << batch_size;
        EXPECT_EQ(result[index].source_port, 8080) << "batch size " << batch_size;
        EXPECT_EQ(result[index].destination_port, 53) << "batch size " << batch_size;
      }
    }
  }

  TEST(CudaPacketParserTest, RejectsMalformedContiguousBatchBeforeDeviceAccess)
  {
    project::PacketBatch batch;
    batch.packet_offsets = { 0 };
    batch.packet_lengths = { 0 };

    project::CudaPacketParser cuda;
    EXPECT_THROW(
        {
          const auto analyses = cuda.parse(batch);
          static_cast<void>(analyses);
        },
        std::invalid_argument);
  }
}
