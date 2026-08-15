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

  std::vector<uint8_t> ipv4_frame(
      const project::MacAddress& destination, const project::MacAddress& source, uint8_t protocol,
      uint16_t transport_size)
  {
    auto bytes = ethernet_frame(destination, source, project::EtherType::IPv4);
    bytes.resize(project::ETHERNET_HEADER_SIZE + 20 + transport_size);
    const size_t ipv4_offset = project::ETHERNET_HEADER_SIZE;
    bytes[ipv4_offset] = 0x45;
    const uint16_t ipv4_size = static_cast<uint16_t>(20 + transport_size);
    bytes[ipv4_offset + 2] = static_cast<uint8_t>(ipv4_size >> 8U);
    bytes[ipv4_offset + 3] = static_cast<uint8_t>(ipv4_size);
    bytes[ipv4_offset + 8] = 64;
    bytes[ipv4_offset + 9] = protocol;
    bytes[ipv4_offset + 12] = 192;
    bytes[ipv4_offset + 13] = 0;
    bytes[ipv4_offset + 14] = 2;
    bytes[ipv4_offset + 15] = 1;
    bytes[ipv4_offset + 16] = 198;
    bytes[ipv4_offset + 17] = 51;
    bytes[ipv4_offset + 18] = 100;
    bytes[ipv4_offset + 19] = 2;
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

  TEST(CpuPacketAnalyzerTest, ExtractsIpv4UdpTcpAndIcmpMetadata)
  {
    const project::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const project::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    auto udp = ipv4_frame(destination, source, 17, 8);
    auto tcp = ipv4_frame(destination, source, 6, 20);
    auto icmp = ipv4_frame(destination, source, 1, 4);
    const size_t udp_offset = project::ETHERNET_HEADER_SIZE + 20;
    udp[udp_offset] = 0x1f;
    udp[udp_offset + 1] = 0x90;
    udp[udp_offset + 2] = 0;
    udp[udp_offset + 3] = 53;
    udp[udp_offset + 4] = 0;
    udp[udp_offset + 5] = 8;
    const size_t tcp_offset = project::ETHERNET_HEADER_SIZE + 20;
    tcp[tcp_offset] = 0x01;
    tcp[tcp_offset + 1] = 0xbb;
    tcp[tcp_offset + 2] = 0x01;
    tcp[tcp_offset + 3] = 0xbd;
    tcp[tcp_offset + 12] = 0x50;
    tcp[tcp_offset + 13] = 0x12;
    const std::array<project::PacketView, 3> packets = {
      project::PacketView{ udp.data(), udp.size() },
      project::PacketView{ tcp.data(), tcp.size() },
      project::PacketView{ icmp.data(), icmp.size() },
    };

    project::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.malformed_packets, 0U);
    EXPECT_EQ(result.packets[0].validity, project::PacketValidity::Valid);
    EXPECT_EQ(result.packets[0].source_ipv4, 0xc0000201U);
    EXPECT_EQ(result.packets[0].destination_ipv4, 0xc6336402U);
    EXPECT_EQ(result.packets[0].protocol, 17);
    EXPECT_EQ(result.packets[0].source_port, 8080);
    EXPECT_EQ(result.packets[0].destination_port, 53);
    EXPECT_EQ(result.packets[1].protocol, 6);
    EXPECT_EQ(result.packets[1].source_port, 443);
    EXPECT_EQ(result.packets[1].destination_port, 445);
    EXPECT_EQ(result.packets[1].tcp_flags, 0x12);
    EXPECT_EQ(result.packets[2].protocol, 1);
  }

  TEST(CpuPacketAnalyzerTest, RejectsTruncatedIpv4AndInvalidUdpHeaders)
  {
    const project::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const project::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    auto truncated_ipv4 = ethernet_frame(destination, source, project::EtherType::IPv4);
    truncated_ipv4.resize(project::ETHERNET_HEADER_SIZE + 20);
    truncated_ipv4[project::ETHERNET_HEADER_SIZE] = 0x45;
    truncated_ipv4[project::ETHERNET_HEADER_SIZE + 2] = 0;
    truncated_ipv4[project::ETHERNET_HEADER_SIZE + 3] = 40;
    auto invalid_udp = ipv4_frame(destination, source, 17, 8);
    const size_t udp_offset = project::ETHERNET_HEADER_SIZE + 20;
    invalid_udp[udp_offset + 4] = 0;
    invalid_udp[udp_offset + 5] = 7;
    const std::array<project::PacketView, 2> packets = {
      project::PacketView{ truncated_ipv4.data(), truncated_ipv4.size() },
      project::PacketView{ invalid_udp.data(), invalid_udp.size() },
    };

    project::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.malformed_packets, 2U);
    EXPECT_EQ(result.packets[0].validity, project::PacketValidity::MalformedIpv4);
    EXPECT_EQ(result.packets[1].validity, project::PacketValidity::MalformedTransport);
    EXPECT_EQ(result.packets[0].classification, project::PacketClassification::UnknownUnicast);
    EXPECT_EQ(result.packets[1].classification, project::PacketClassification::UnknownUnicast);
  }
}
