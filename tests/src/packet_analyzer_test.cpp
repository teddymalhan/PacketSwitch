#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "project/packet_analyzer.hpp"

namespace
{
  std::vector<uint8_t> ethernet_frame(
      const project::MacAddress& destination, const project::MacAddress& source, uint16_t ethertype)
  {
    std::vector<uint8_t> bytes(project::ETHERNET_HEADER_SIZE);
    std::copy(destination.bytes().begin(), destination.bytes().end(), bytes.begin());
    std::copy(source.bytes().begin(), source.bytes().end(), bytes.begin() + project::MAC_ADDRESS_SIZE);
    bytes[12] = static_cast<uint8_t>(ethertype >> 8U);
    bytes[13] = static_cast<uint8_t>(ethertype);
    return bytes;
  }

  TEST(CpuPacketAnalyzerTest, ClassifiesPacketsAndLearnsSourcesInOrder)
  {
    const project::MacAddress first_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const project::MacAddress second_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const auto first = ethernet_frame(second_source, first_source, project::EtherType::IPv4);
    const auto second = ethernet_frame(first_source, second_source, project::EtherType::ARP);
    const auto broadcast = ethernet_frame(project::MacAddress::broadcast(), first_source, project::EtherType::IPv4);
    const std::array<project::PacketView, 3> packets = {
      project::PacketView{ first.data(), first.size() },
      project::PacketView{ second.data(), second.size() },
      project::PacketView{ broadcast.data(), broadcast.size() },
    };

    project::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.packets[0].classification, project::PacketClassification::UnknownUnicast);
    EXPECT_EQ(result.packets[1].classification, project::PacketClassification::KnownUnicast);
    EXPECT_EQ(result.packets[2].classification, project::PacketClassification::Broadcast);
    EXPECT_EQ(result.packets[1].ethertype, project::EtherType::ARP);
    EXPECT_EQ(result.received_packets, 3U);
    EXPECT_EQ(result.received_bytes, 3U * project::ETHERNET_HEADER_SIZE);
    EXPECT_EQ(result.unknown_unicast_packets, 1U);
    EXPECT_EQ(result.known_unicast_packets, 1U);
    EXPECT_EQ(result.broadcast_packets, 1U);
  }

  TEST(CpuPacketAnalyzerTest, RecordsMalformedPacketsWithoutReadingTheirPayload)
  {
    const std::array<uint8_t, project::ETHERNET_HEADER_SIZE - 1> truncated{};
    const std::array<project::PacketView, 2> packets = {
      project::PacketView{ nullptr, 0 },
      project::PacketView{ truncated.data(), truncated.size() },
    };

    project::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.packets[0].classification, project::PacketClassification::Malformed);
    EXPECT_EQ(result.packets[1].classification, project::PacketClassification::Malformed);
    EXPECT_EQ(result.malformed_packets, 2U);
    EXPECT_EQ(result.received_bytes, truncated.size());
  }

  TEST(CpuPacketAnalyzerTest, ResetClearsLearnedMacAddresses)
  {
    const project::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const project::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const auto first = ethernet_frame(destination, source, project::EtherType::IPv4);
    const auto second = ethernet_frame(source, destination, project::EtherType::IPv4);
    const std::array<project::PacketView, 2> learn_packets = {
      project::PacketView{ first.data(), first.size() },
      project::PacketView{ second.data(), second.size() },
    };
    const std::array<project::PacketView, 1> known_packet = { project::PacketView{ first.data(), first.size() } };

    project::CpuPacketAnalyzer analyzer;
    const auto learned = analyzer.analyze(learn_packets.data(), learn_packets.size());
    EXPECT_EQ(learned.unknown_unicast_packets, 1U);
    EXPECT_EQ(learned.known_unicast_packets, 1U);
    EXPECT_EQ(analyzer.analyze(known_packet.data(), known_packet.size()).known_unicast_packets, 1U);

    analyzer.reset();
    EXPECT_EQ(analyzer.analyze(known_packet.data(), known_packet.size()).unknown_unicast_packets, 1U);
  }
}
