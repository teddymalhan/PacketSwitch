#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "wirelab/packet_analyzer.hpp"
#include "wirelab/packet_batch.hpp"


namespace
{
  std::vector<uint8_t> ethernet_frame(
      const wirelab::MacAddress& destination, const wirelab::MacAddress& source, uint16_t ethertype)
  {
    std::vector<uint8_t> bytes(wirelab::ETHERNET_HEADER_SIZE);
    std::copy(destination.bytes().begin(), destination.bytes().end(), bytes.begin());
    std::copy(source.bytes().begin(), source.bytes().end(), bytes.begin() + wirelab::MAC_ADDRESS_SIZE);
    bytes[12] = static_cast<uint8_t>(ethertype >> 8U);
    bytes[13] = static_cast<uint8_t>(ethertype);
    return bytes;
  }

  std::vector<uint8_t> ipv4_frame(
      const wirelab::MacAddress& destination, const wirelab::MacAddress& source, uint8_t protocol,
      uint16_t transport_size)
  {
    auto bytes = ethernet_frame(destination, source, wirelab::EtherType::IPv4);
    bytes.resize(wirelab::ETHERNET_HEADER_SIZE + 20 + transport_size);
    const size_t ipv4_offset = wirelab::ETHERNET_HEADER_SIZE;
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
    const wirelab::MacAddress first_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress second_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const auto first = ethernet_frame(second_source, first_source, wirelab::EtherType::IPv4);
    const auto second = ethernet_frame(first_source, second_source, wirelab::EtherType::ARP);
    const auto broadcast = ethernet_frame(wirelab::MacAddress::broadcast(), first_source, wirelab::EtherType::IPv4);
    const std::array<wirelab::PacketView, 3> packets = {
      wirelab::PacketView{ first.data(), first.size(), 7 },
      wirelab::PacketView{ second.data(), second.size() },
      wirelab::PacketView{ broadcast.data(), broadcast.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.packets[0].classification, wirelab::PacketClassification::UnknownUnicast);
    EXPECT_EQ(result.packets[1].classification, wirelab::PacketClassification::KnownUnicast);
    EXPECT_EQ(result.packets[2].classification, wirelab::PacketClassification::Broadcast);
    EXPECT_EQ(result.packets[1].ethertype, wirelab::EtherType::ARP);
    EXPECT_EQ(result.packets[0].ingress_port, 7U);
    EXPECT_EQ(result.received_packets, 3U);
    EXPECT_EQ(result.received_bytes, 3U * wirelab::ETHERNET_HEADER_SIZE);
    EXPECT_EQ(result.unknown_unicast_packets, 1U);
    EXPECT_EQ(result.known_unicast_packets, 1U);
    EXPECT_EQ(result.broadcast_packets, 1U);
  }

  TEST(CpuPacketAnalyzerTest, AnalyzesContiguousPacketBatchesWithEquivalentResults)
  {
    const wirelab::MacAddress first_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress second_source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const auto first = ethernet_frame(second_source, first_source, wirelab::EtherType::IPv4);
    const auto second = ethernet_frame(first_source, second_source, wirelab::EtherType::ARP);
    const auto broadcast = ethernet_frame(wirelab::MacAddress::broadcast(), first_source, wirelab::EtherType::IPv4);
    const std::array<wirelab::PacketView, 3> packets = {
      wirelab::PacketView{ first.data(), first.size(), 7 },
      wirelab::PacketView{ second.data(), second.size(), 8 },
      wirelab::PacketView{ broadcast.data(), broadcast.size(), 9 },
    };
    const auto batch = wirelab::PacketBatch::create(packets.data(), packets.size(), 1234);

    ASSERT_TRUE(batch.has_value());
    wirelab::CpuPacketAnalyzer direct_analyzer;
    wirelab::CpuPacketAnalyzer batch_analyzer;
    const auto direct_result = direct_analyzer.analyze(packets.data(), packets.size());
    const auto batch_result = batch_analyzer.analyze(*batch);

    EXPECT_EQ(batch_result.received_packets, direct_result.received_packets);
    EXPECT_EQ(batch_result.received_bytes, direct_result.received_bytes);
    EXPECT_EQ(batch_result.malformed_packets, direct_result.malformed_packets);
    EXPECT_EQ(batch_result.broadcast_packets, direct_result.broadcast_packets);
    EXPECT_EQ(batch_result.unknown_unicast_packets, direct_result.unknown_unicast_packets);
    EXPECT_EQ(batch_result.known_unicast_packets, direct_result.known_unicast_packets);
    ASSERT_EQ(batch_result.packets.size(), direct_result.packets.size());
    for (size_t index = 0; index < direct_result.packets.size(); ++index)
    {
      EXPECT_EQ(batch_result.packets[index].source_mac, direct_result.packets[index].source_mac);
      EXPECT_EQ(batch_result.packets[index].destination_mac, direct_result.packets[index].destination_mac);
      EXPECT_EQ(batch_result.packets[index].ingress_port, direct_result.packets[index].ingress_port);
      EXPECT_EQ(batch_result.packets[index].classification, direct_result.packets[index].classification);
      EXPECT_EQ(batch_result.packets[index].validity, direct_result.packets[index].validity);
    }
  }

  TEST(CpuPacketAnalyzerTest, RecordsMalformedPacketsWithoutReadingTheirPayload)
  {
    const std::array<uint8_t, wirelab::ETHERNET_HEADER_SIZE - 1> truncated{};
    const std::array<wirelab::PacketView, 2> packets = {
      wirelab::PacketView{ nullptr, 0 },
      wirelab::PacketView{ truncated.data(), truncated.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.packets[0].classification, wirelab::PacketClassification::Malformed);
    EXPECT_EQ(result.packets[1].classification, wirelab::PacketClassification::Malformed);
    EXPECT_EQ(result.malformed_packets, 2U);
    EXPECT_EQ(result.received_bytes, truncated.size());
  }

  TEST(CpuPacketAnalyzerTest, ResetClearsLearnedMacAddresses)
  {
    const wirelab::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const auto first = ethernet_frame(destination, source, wirelab::EtherType::IPv4);
    const auto second = ethernet_frame(source, destination, wirelab::EtherType::IPv4);
    const std::array<wirelab::PacketView, 2> learn_packets = {
      wirelab::PacketView{ first.data(), first.size() },
      wirelab::PacketView{ second.data(), second.size() },
    };
    const std::array<wirelab::PacketView, 1> known_packet = { wirelab::PacketView{ first.data(), first.size() } };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto learned = analyzer.analyze(learn_packets.data(), learn_packets.size());
    EXPECT_EQ(learned.unknown_unicast_packets, 1U);
    EXPECT_EQ(learned.known_unicast_packets, 1U);
    EXPECT_EQ(analyzer.analyze(known_packet.data(), known_packet.size()).known_unicast_packets, 1U);

    analyzer.reset();
    EXPECT_EQ(analyzer.analyze(known_packet.data(), known_packet.size()).unknown_unicast_packets, 1U);
  }

  TEST(CpuPacketAnalyzerTest, ExtractsIpv4UdpTcpAndIcmpMetadata)
  {
    const wirelab::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    auto udp = ipv4_frame(destination, source, 17, 8);
    auto tcp = ipv4_frame(destination, source, 6, 20);
    auto icmp = ipv4_frame(destination, source, 1, 4);
    const size_t udp_offset = wirelab::ETHERNET_HEADER_SIZE + 20;
    udp[udp_offset] = 0x1f;
    udp[udp_offset + 1] = 0x90;
    udp[udp_offset + 2] = 0;
    udp[udp_offset + 3] = 53;
    udp[udp_offset + 4] = 0;
    udp[udp_offset + 5] = 8;
    const size_t tcp_offset = wirelab::ETHERNET_HEADER_SIZE + 20;
    tcp[tcp_offset] = 0x01;
    tcp[tcp_offset + 1] = 0xbb;
    tcp[tcp_offset + 2] = 0x01;
    tcp[tcp_offset + 3] = 0xbd;
    tcp[tcp_offset + 12] = 0x50;
    tcp[tcp_offset + 13] = 0x12;
    const std::array<wirelab::PacketView, 3> packets = {
      wirelab::PacketView{ udp.data(), udp.size() },
      wirelab::PacketView{ tcp.data(), tcp.size() },
      wirelab::PacketView{ icmp.data(), icmp.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.malformed_packets, 0U);
    EXPECT_EQ(result.packets[0].validity, wirelab::PacketValidity::Valid);
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
    const wirelab::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    auto truncated_ipv4 = ethernet_frame(destination, source, wirelab::EtherType::IPv4);
    truncated_ipv4.resize(wirelab::ETHERNET_HEADER_SIZE + 20);
    truncated_ipv4[wirelab::ETHERNET_HEADER_SIZE] = 0x45;
    truncated_ipv4[wirelab::ETHERNET_HEADER_SIZE + 2] = 0;
    truncated_ipv4[wirelab::ETHERNET_HEADER_SIZE + 3] = 40;
    auto invalid_udp = ipv4_frame(destination, source, 17, 8);
    const size_t udp_offset = wirelab::ETHERNET_HEADER_SIZE + 20;
    invalid_udp[udp_offset + 4] = 0;
    invalid_udp[udp_offset + 5] = 7;
    const std::array<wirelab::PacketView, 2> packets = {
      wirelab::PacketView{ truncated_ipv4.data(), truncated_ipv4.size() },
      wirelab::PacketView{ invalid_udp.data(), invalid_udp.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.packets.size(), packets.size());
    EXPECT_EQ(result.malformed_packets, 2U);
    EXPECT_EQ(result.packets[0].validity, wirelab::PacketValidity::MalformedIpv4);
    EXPECT_EQ(result.packets[1].validity, wirelab::PacketValidity::MalformedTransport);
    EXPECT_EQ(result.packets[0].classification, wirelab::PacketClassification::UnknownUnicast);
    EXPECT_EQ(result.packets[1].classification, wirelab::PacketClassification::UnknownUnicast);
  }

  TEST(CpuPacketAnalyzerTest, AggregatesValidIpv4PacketsByFiveTuple)
  {
    const wirelab::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    auto first = ipv4_frame(destination, source, 17, 8);
    auto second = first;
    auto different_flow = ipv4_frame(destination, source, 17, 8);
    const size_t transport_offset = wirelab::ETHERNET_HEADER_SIZE + 20;
    first[transport_offset] = 0x1f;
    first[transport_offset + 1] = 0x90;
    first[transport_offset + 2] = 0;
    first[transport_offset + 3] = 53;
    first[transport_offset + 4] = 0;
    first[transport_offset + 5] = 8;
    second = first;
    different_flow[transport_offset] = 0;
    different_flow[transport_offset + 1] = 80;
    different_flow[transport_offset + 2] = 0x01;
    different_flow[transport_offset + 3] = 0xbb;
    different_flow[transport_offset + 4] = 0;
    different_flow[transport_offset + 5] = 8;
    const std::array<wirelab::PacketView, 3> packets = {
      wirelab::PacketView{ first.data(), first.size() },
      wirelab::PacketView{ second.data(), second.size() },
      wirelab::PacketView{ different_flow.data(), different_flow.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.flows.size(), 2U);
    EXPECT_EQ(result.flows[0].key.source_port, 8080);
    EXPECT_EQ(result.flows[0].key.destination_port, 53);
    EXPECT_EQ(result.flows[0].packet_count, 2U);
    EXPECT_EQ(result.flows[0].byte_count, first.size() + second.size());
    EXPECT_EQ(result.flows[1].key.source_port, 80);
    EXPECT_EQ(result.flows[1].key.destination_port, 443);
    EXPECT_EQ(result.flows[1].packet_count, 1U);
    EXPECT_EQ(result.flows[1].byte_count, different_flow.size());
    EXPECT_NE(result.flows[0].flow_hash, result.flows[1].flow_hash);
    EXPECT_EQ(result.packets[0].flow_hash, result.packets[1].flow_hash);
    EXPECT_EQ(result.packets[0].flow_hash, result.flows[0].flow_hash);
    ASSERT_EQ(result.frame_size_histogram.size(), 7U);
    const auto frame_bucket = std::find_if(
        result.frame_size_histogram.begin(), result.frame_size_histogram.end(),
        [&first](const wirelab::PacketSizeHistogramBucket& bucket) {
          return first.size() >= bucket.inclusive_minimum && first.size() <= bucket.inclusive_maximum;
        });
    ASSERT_NE(frame_bucket, result.frame_size_histogram.end());
    EXPECT_EQ(frame_bucket->packet_count, 3U);
    EXPECT_EQ(frame_bucket->byte_count, first.size() + second.size() + different_flow.size());

    ASSERT_EQ(result.ethertype_histogram.size(), 1U);
    EXPECT_EQ(result.ethertype_histogram[0].value, wirelab::EtherType::IPv4);
    EXPECT_EQ(result.ethertype_histogram[0].packet_count, 3U);
    EXPECT_EQ(result.ethertype_histogram[0].byte_count, first.size() + second.size() + different_flow.size());

    ASSERT_EQ(result.protocol_histogram.size(), 1U);
    EXPECT_EQ(result.protocol_histogram[0].value, 17U);
    EXPECT_EQ(result.protocol_histogram[0].packet_count, 3U);
    EXPECT_EQ(result.protocol_histogram[0].byte_count, first.size() + second.size() + different_flow.size());

    ASSERT_EQ(result.destination_port_histogram.size(), 2U);
    EXPECT_EQ(result.destination_port_histogram[0].value, 53U);
    EXPECT_EQ(result.destination_port_histogram[0].packet_count, 2U);
    EXPECT_EQ(result.destination_port_histogram[1].value, 443U);
    EXPECT_EQ(result.destination_port_histogram[1].packet_count, 1U);
  }

  TEST(CpuPacketAnalyzerTest, AggregatesMacTrafficAndTrafficMatrix)
  {
    const wirelab::MacAddress source_a({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source_b({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const wirelab::MacAddress destination_a({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 });
    const wirelab::MacAddress destination_b({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x11 });
    auto first = ethernet_frame(destination_a, source_a, wirelab::EtherType::ARP);
    auto second = ethernet_frame(destination_b, source_a, wirelab::EtherType::ARP);
    auto third = ethernet_frame(destination_a, source_b, wirelab::EtherType::ARP);
    second.resize(second.size() + 7);
    third.resize(third.size() + 13);
    const std::array<wirelab::PacketView, 3> packets = {
      wirelab::PacketView{ first.data(), first.size() },
      wirelab::PacketView{ second.data(), second.size() },
      wirelab::PacketView{ third.data(), third.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.source_mac_traffic.size(), 2U);
    EXPECT_EQ(result.source_mac_traffic[0].mac, source_a);
    EXPECT_EQ(result.source_mac_traffic[0].packet_count, 2U);
    EXPECT_EQ(result.source_mac_traffic[0].byte_count, first.size() + second.size());
    EXPECT_EQ(result.source_mac_traffic[1].mac, source_b);
    EXPECT_EQ(result.source_mac_traffic[1].packet_count, 1U);
    EXPECT_EQ(result.source_mac_traffic[1].byte_count, third.size());

    ASSERT_EQ(result.destination_mac_traffic.size(), 2U);
    EXPECT_EQ(result.destination_mac_traffic[0].mac, destination_a);
    EXPECT_EQ(result.destination_mac_traffic[0].packet_count, 2U);
    EXPECT_EQ(result.destination_mac_traffic[0].byte_count, first.size() + third.size());
    EXPECT_EQ(result.destination_mac_traffic[1].mac, destination_b);
    EXPECT_EQ(result.destination_mac_traffic[1].packet_count, 1U);
    EXPECT_EQ(result.destination_mac_traffic[1].byte_count, second.size());

    ASSERT_EQ(result.mac_traffic_matrix.size(), 3U);
    EXPECT_EQ(result.mac_traffic_matrix[0].source_mac, source_a);
    EXPECT_EQ(result.mac_traffic_matrix[0].destination_mac, destination_a);
    EXPECT_EQ(result.mac_traffic_matrix[0].packet_count, 1U);
    EXPECT_EQ(result.mac_traffic_matrix[0].byte_count, first.size());
    EXPECT_EQ(result.mac_traffic_matrix[1].source_mac, source_a);
    EXPECT_EQ(result.mac_traffic_matrix[1].destination_mac, destination_b);
    EXPECT_EQ(result.mac_traffic_matrix[1].packet_count, 1U);
    EXPECT_EQ(result.mac_traffic_matrix[1].byte_count, second.size());
    EXPECT_EQ(result.mac_traffic_matrix[2].source_mac, source_b);
    EXPECT_EQ(result.mac_traffic_matrix[2].destination_mac, destination_a);
    EXPECT_EQ(result.mac_traffic_matrix[2].packet_count, 1U);
    EXPECT_EQ(result.mac_traffic_matrix[2].byte_count, third.size());
  }

  TEST(CpuPacketAnalyzerTest, RanksMacTrafficByBytesPacketsAndMacAddress)
  {
    const wirelab::MacAddress destination({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 });
    const wirelab::MacAddress source_a({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 });
    const wirelab::MacAddress source_b({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 });
    const wirelab::MacAddress source_c({ 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 });
    const auto first_a = ethernet_frame(destination, source_a, wirelab::EtherType::ARP);
    const auto second_a = ethernet_frame(destination, source_a, wirelab::EtherType::ARP);
    auto from_b = ethernet_frame(destination, source_b, wirelab::EtherType::ARP);
    auto from_c = ethernet_frame(destination, source_c, wirelab::EtherType::ARP);
    from_b.resize(from_b.size() + 15);
    from_c.resize(from_c.size() + 15);
    const std::array<wirelab::PacketView, 4> packets = {
      wirelab::PacketView{ first_a.data(), first_a.size() },
      wirelab::PacketView{ second_a.data(), second_a.size() },
      wirelab::PacketView{ from_c.data(), from_c.size() },
      wirelab::PacketView{ from_b.data(), from_b.size() },
    };

    wirelab::CpuPacketAnalyzer analyzer;
    const auto result = analyzer.analyze(packets.data(), packets.size());

    ASSERT_EQ(result.source_mac_traffic.size(), 3U);
    EXPECT_EQ(result.source_mac_traffic[0].mac, source_b);
    EXPECT_EQ(result.source_mac_traffic[0].byte_count, from_b.size());
    EXPECT_EQ(result.source_mac_traffic[1].mac, source_c);
    EXPECT_EQ(result.source_mac_traffic[1].byte_count, from_c.size());
    EXPECT_EQ(result.source_mac_traffic[2].mac, source_a);
    EXPECT_EQ(result.source_mac_traffic[2].packet_count, 2U);
  }
}
