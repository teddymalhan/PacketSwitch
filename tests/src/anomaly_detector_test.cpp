#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "project/anomaly_detector.hpp"

namespace
{
  project::PacketAnalysis packet(project::PacketClassification classification, project::MacAddress source_mac,
                                 uint32_t source_ipv4 = 0, uint8_t protocol = 0, uint16_t destination_port = 0,
                                 uint32_t ingress_port = 0)
  {
    project::PacketAnalysis result;
    result.classification = classification;
    result.source_mac = source_mac;
    result.source_ipv4 = source_ipv4;
    result.protocol = protocol;
    result.destination_port = destination_port;
    result.ingress_port = ingress_port;
    result.frame_length = 60;
    result.validity = protocol == 0 ? project::PacketValidity::MalformedEthernet : project::PacketValidity::Valid;
    return result;
  }

  project::AnomalyDetectorConfig configured_detector()
  {
    project::AnomalyDetectorConfig config;
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
    const project::MacAddress broadcast_source({ 0, 0, 0, 0, 0, 1 });
    const project::MacAddress unknown_source({ 0, 0, 0, 0, 0, 2 });
    const project::MacAddress flapping_source({ 0, 0, 0, 0, 0, 3 });
    project::AnalysisBatch batch;
    for (size_t index = 0; index < 3; ++index)
    {
      batch.packets.push_back(packet(project::PacketClassification::Broadcast, broadcast_source));
      batch.packets.push_back(packet(project::PacketClassification::UnknownUnicast, unknown_source));
      batch.packets.push_back(packet(project::PacketClassification::KnownUnicast, project::MacAddress(), 0xc0000201U,
                                     17, static_cast<uint16_t>(100 + index)));
    }
    batch.packets.push_back(packet(project::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 1));
    batch.packets.push_back(packet(project::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 2));
    batch.packets.push_back(packet(project::PacketClassification::KnownUnicast, flapping_source, 0, 0, 0, 1));

    project::AnomalyDetector detector(configured_detector());
    const auto events = detector.evaluate(batch, 0);

    ASSERT_EQ(events.size(), 5U);
    EXPECT_EQ(events[0].type, project::AnomalyType::BroadcastStorm);
    EXPECT_EQ(events[0].source_mac, broadcast_source);
    EXPECT_EQ(events[0].observed_packets, 3U);
    EXPECT_EQ(events[0].observed_bytes, 180U);
    EXPECT_EQ(events[1].type, project::AnomalyType::MacFlap);
    EXPECT_EQ(events[1].source_mac, flapping_source);
    EXPECT_EQ(events[1].ingress_port, 1U);
    EXPECT_EQ(events[1].observed_packets, 2U);
    EXPECT_EQ(events[2].type, project::AnomalyType::UnknownUnicastFlood);
    EXPECT_EQ(events[2].source_mac, unknown_source);
    EXPECT_EQ(events[3].type, project::AnomalyType::UdpFlood);
    EXPECT_EQ(events[3].source_ipv4, 0xc0000201U);
    EXPECT_EQ(events[3].observed_packets, 3U);
    EXPECT_EQ(events[4].type, project::AnomalyType::PortScan);
    EXPECT_EQ(events[4].source_ipv4, 0xc0000201U);
    EXPECT_EQ(events[4].observed_distinct_destinations, 3U);
  }

  TEST(AnomalyDetectorTest, EmitsOnlyThresholdCrossingsAndRearmsAfterWindowExpiry)
  {
    const project::MacAddress source({ 0, 0, 0, 0, 0, 1 });
    project::AnalysisBatch batch;
    batch.packets.push_back(packet(project::PacketClassification::Broadcast, source));
    batch.packets.push_back(packet(project::PacketClassification::Broadcast, source));

    auto config = configured_detector();
    config.broadcast_packets_threshold = 1;
    config.mac_flap_transitions_threshold = 0;
    config.unknown_unicast_packets_threshold = 0;
    config.udp_packets_threshold = 0;
    config.port_scan_destinations_threshold = 0;
    project::AnomalyDetector detector(config);

    ASSERT_EQ(detector.evaluate(batch, 0).size(), 1U);
    EXPECT_TRUE(detector.evaluate(project::AnalysisBatch(), 500).empty());
    EXPECT_TRUE(detector.evaluate(project::AnalysisBatch(), 1'001).empty());
    ASSERT_EQ(detector.evaluate(batch, 1'002).size(), 1U);
  }

  TEST(AnomalyDetectorTest, DetectsHotTalkersAndMalformedFramesPerIngressPort)
  {
    const project::MacAddress hot_talker({ 0, 0, 0, 0, 0, 9 });
    project::AnalysisBatch batch;
    for (size_t index = 0; index < 3; ++index)
    {
      batch.packets.push_back(packet(project::PacketClassification::KnownUnicast, hot_talker, 0xc0000209U, 1));
    }
    batch.packets.push_back(packet(project::PacketClassification::Malformed, project::MacAddress(), 0, 0, 0, 7));
    batch.packets.push_back(packet(project::PacketClassification::Malformed, project::MacAddress(), 0, 0, 0, 7));
    batch.packets.push_back(packet(project::PacketClassification::Malformed, project::MacAddress(), 0, 0, 0, 8));

    auto config = configured_detector();
    config.broadcast_packets_threshold = 0;
    config.mac_flap_transitions_threshold = 0;
    config.unknown_unicast_packets_threshold = 0;
    config.udp_packets_threshold = 0;
    config.port_scan_destinations_threshold = 0;
    config.hot_talker_packets_threshold = 2;
    config.malformed_frames_threshold = 1;

    project::AnomalyDetector detector(config);
    const auto events = detector.evaluate(batch, 0);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].type, project::AnomalyType::HotTalker);
    EXPECT_EQ(events[0].source_mac, hot_talker);
    EXPECT_EQ(events[0].observed_packets, 3U);
    EXPECT_EQ(events[0].observed_bytes, 180U);
    EXPECT_EQ(events[1].type, project::AnomalyType::MalformedFrame);
    EXPECT_EQ(events[1].ingress_port, 7U);
    EXPECT_EQ(events[1].observed_packets, 2U);
    EXPECT_EQ(events[1].observed_bytes, 120U);
  }

  TEST(AnomalyDetectorTest, RejectsInvalidWindowAndOutOfOrderTimestamps)
  {
    project::AnomalyDetectorConfig config;
    config.window_duration_ns = 0;
    EXPECT_THROW(project::AnomalyDetector detector(config), std::invalid_argument);

    config.window_duration_ns = 1;
    project::AnomalyDetector detector(config);
    EXPECT_TRUE(detector.evaluate(project::AnalysisBatch(), 2).empty());
    EXPECT_THROW(static_cast<void>(detector.evaluate(project::AnalysisBatch(), 1)), std::invalid_argument);
  }
}
