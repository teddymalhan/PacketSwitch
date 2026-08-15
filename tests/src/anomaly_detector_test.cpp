#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "wirelab/anomaly_detector.hpp"

namespace
{
  wirelab::PacketAnalysis packet(wirelab::PacketClassification classification, wirelab::MacAddress source_mac,
                                 uint32_t source_ipv4 = 0, uint8_t protocol = 0, uint16_t destination_port = 0,
                                 uint32_t ingress_port = 0)
  {
    wirelab::PacketAnalysis result;
    result.classification = classification;
    result.source_mac = source_mac;
    result.source_ipv4 = source_ipv4;
    result.protocol = protocol;
    result.destination_port = destination_port;
    result.ingress_port = ingress_port;
    result.frame_length = 60;
    result.validity = protocol == 0 ? wirelab::PacketValidity::MalformedEthernet : wirelab::PacketValidity::Valid;
    return result;
  }

  wirelab::AnomalyDetectorConfig configured_detector()
  {
    wirelab::AnomalyDetectorConfig config;
    config.window_duration_ns = 1'000;
    config.broadcast_packets_threshold = 2;
    config.mac_flap_transitions_threshold = 1;
    config.unknown_unicast_packets_threshold = 2;
    config.udp_packets_threshold = 2;
    config.port_scan_destinations_threshold = 2;
    config.hot_talker_packets_threshold = 0;
    config.malformed_frames_threshold = 0;
    return config;
  }

  TEST(AnomalyDetectorTest, DetectsConfiguredThresholdViolationsWithEvidence)
  {
    const wirelab::MacAddress broadcast_source({ 0, 0, 0, 0, 0, 1 });
    const wirelab::MacAddress unknown_source({ 0, 0, 0, 0, 0, 2 });
    const wirelab::MacAddress flapping_source({ 0, 0, 0, 0, 0, 3 });
    wirelab::AnalysisBatch batch;
    for (size_t index = 0; index < 3; ++index)
    {
      batch.packets.push_back(packet(wirelab::PacketClassification::Broadcast, broadcast_source));
      batch.packets.push_back(packet(wirelab::PacketClassification::UnknownUnicast, unknown_source));
      batch.packets.push_back(packet(wirelab::PacketClassification::KnownUnicast, wirelab::MacAddress(), 0xc0000201U,
                                     17, static_cast<uint16_t>(100 + index)));
    }
    batch.packets.push_back(packet(wirelab::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 1));
    batch.packets.push_back(packet(wirelab::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 2));
    batch.packets.push_back(packet(wirelab::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 1));

    wirelab::AnomalyDetector detector(configured_detector());
    const auto events = detector.evaluate(batch, 0);

    ASSERT_EQ(events.size(), 5U);
    EXPECT_EQ(events[0].type, wirelab::AnomalyType::BroadcastStorm);
    EXPECT_EQ(events[0].source_mac, broadcast_source);
    EXPECT_EQ(events[0].observed_packets, 3U);
    EXPECT_EQ(events[0].observed_bytes, 180U);
    EXPECT_EQ(events[1].type, wirelab::AnomalyType::MacFlap);
    EXPECT_EQ(events[1].source_mac, flapping_source);
    EXPECT_EQ(events[1].ingress_port, 1U);
    EXPECT_EQ(events[1].observed_packets, 2U);
    EXPECT_EQ(events[2].type, wirelab::AnomalyType::UnknownUnicastFlood);
    EXPECT_EQ(events[2].source_mac, unknown_source);
    EXPECT_EQ(events[3].type, wirelab::AnomalyType::UdpFlood);
    EXPECT_EQ(events[3].source_ipv4, 0xc0000201U);
    EXPECT_EQ(events[3].observed_packets, 3U);
    EXPECT_EQ(events[4].type, wirelab::AnomalyType::PortScan);
    EXPECT_EQ(events[4].source_ipv4, 0xc0000201U);
    EXPECT_EQ(events[4].observed_distinct_destinations, 3U);
  }

  TEST(AnomalyDetectorTest, EmitsOnlyThresholdCrossingsAndRearmsAfterWindowExpiry)
  {
    const wirelab::MacAddress source({ 0, 0, 0, 0, 0, 1 });
    wirelab::AnalysisBatch batch;
    batch.packets.push_back(packet(wirelab::PacketClassification::Broadcast, source));
    batch.packets.push_back(packet(wirelab::PacketClassification::Broadcast, source));

    auto config = configured_detector();
    config.broadcast_packets_threshold = 1;
    config.mac_flap_transitions_threshold = 0;
    config.unknown_unicast_packets_threshold = 0;
    config.udp_packets_threshold = 0;
    config.port_scan_destinations_threshold = 0;
    wirelab::AnomalyDetector detector(config);

    ASSERT_EQ(detector.evaluate(batch, 0).size(), 1U);
    EXPECT_TRUE(detector.evaluate(wirelab::AnalysisBatch(), 500).empty());
    EXPECT_TRUE(detector.evaluate(wirelab::AnalysisBatch(), 1'001).empty());
    ASSERT_EQ(detector.evaluate(batch, 1'002).size(), 1U);
  }

  TEST(AnomalyDetectorTest, DetectsHotTalkersAndMalformedFramesPerIngressPort)
  {
    const wirelab::MacAddress hot_talker({ 0, 0, 0, 0, 0, 9 });
    wirelab::AnalysisBatch batch;
    for (size_t index = 0; index < 3; ++index)
    {
      batch.packets.push_back(packet(wirelab::PacketClassification::KnownUnicast, hot_talker, 0xc0000209U, 1));
    }
    batch.packets.push_back(packet(wirelab::PacketClassification::Malformed, wirelab::MacAddress(), 0, 0, 0, 7));
    batch.packets.push_back(packet(wirelab::PacketClassification::Malformed, wirelab::MacAddress(), 0, 0, 0, 7));
    batch.packets.push_back(packet(wirelab::PacketClassification::Malformed, wirelab::MacAddress(), 0, 0, 0, 8));

    auto config = configured_detector();
    config.broadcast_packets_threshold = 0;
    config.mac_flap_transitions_threshold = 0;
    config.unknown_unicast_packets_threshold = 0;
    config.udp_packets_threshold = 0;
    config.port_scan_destinations_threshold = 0;
    config.hot_talker_packets_threshold = 2;
    config.malformed_frames_threshold = 1;

    wirelab::AnomalyDetector detector(config);
    const auto events = detector.evaluate(batch, 0);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].type, wirelab::AnomalyType::HotTalker);
    EXPECT_EQ(events[0].source_mac, hot_talker);
    EXPECT_EQ(events[0].observed_packets, 3U);
    EXPECT_EQ(events[0].observed_bytes, 180U);
    EXPECT_EQ(events[1].type, wirelab::AnomalyType::MalformedFrame);
    EXPECT_EQ(events[1].ingress_port, 7U);
    EXPECT_EQ(events[1].observed_packets, 2U);
    EXPECT_EQ(events[1].observed_bytes, 120U);
  }

  TEST(AnomalyDetectorTest, RejectsInvalidWindowAndOutOfOrderTimestamps)
  {
    wirelab::AnomalyDetectorConfig config;
    config.window_duration_ns = 0;
    EXPECT_THROW(wirelab::AnomalyDetector detector(config), std::invalid_argument);

    config.window_duration_ns = 1;
    wirelab::AnomalyDetector detector(config);
    EXPECT_TRUE(detector.evaluate(wirelab::AnalysisBatch(), 2).empty());
    EXPECT_THROW(static_cast<void>(detector.evaluate(wirelab::AnalysisBatch(), 1)), std::invalid_argument);
  }
}
