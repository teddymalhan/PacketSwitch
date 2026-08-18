#include "wirelab/traffic_generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/ethernet_frame.hpp"
#include "wirelab/packet_analyzer.hpp"

namespace
{
  constexpr size_t SCENARIO_FRAME_COUNT = 400;

  constexpr wirelab::TrafficScenario ALL_SCENARIOS[] = {
    wirelab::TrafficScenario::KnownUnicast,  wirelab::TrafficScenario::Broadcast, wirelab::TrafficScenario::UnknownUnicast,
    wirelab::TrafficScenario::Mixed,         wirelab::TrafficScenario::UdpFlood,  wirelab::TrafficScenario::PortScan,
    wirelab::TrafficScenario::BroadcastStorm
  };

  // The thresholds vswitch_main installs on a live switch, so a scenario that
  // trips here trips the shipped configuration too.
  wirelab::AnomalyDetectorConfig live_anomaly_config()
  {
    wirelab::AnomalyDetectorConfig config;
    config.window_duration_ns = 1'000'000'000;
    config.broadcast_packets_threshold = 100;
    config.unknown_unicast_packets_threshold = 100;
    config.udp_packets_threshold = 200;
    config.port_scan_destinations_threshold = 20;
    config.hot_talker_packets_threshold = 200;
    config.malformed_frames_threshold = 10;
    return config;
  }

  std::vector<wirelab::AnomalyEvent> scenario_anomalies(wirelab::TrafficScenario scenario)
  {
    wirelab::TrafficGeneratorConfig config;
    config.scenario = scenario;
    config.seed = 7;
    config.frame_size = 128;

    wirelab::DeterministicTrafficGenerator generator(config);
    const auto frames = generator.generate(SCENARIO_FRAME_COUNT);
    std::vector<wirelab::PacketView> views;
    views.reserve(frames.size());
    for (const auto& frame : frames)
    {
      views.push_back(wirelab::PacketView{ frame.data(), frame.size(), 1 });
    }

    wirelab::CpuPacketAnalyzer analyzer;
    const auto batch = analyzer.analyze(views.data(), views.size());
    EXPECT_EQ(batch.malformed_packets, 0U);

    wirelab::AnalysisPipeline pipeline(live_anomaly_config());
    return pipeline.evaluate(batch, 1'000'000, std::chrono::steady_clock::now()).anomalies;
  }

  bool contains(const std::vector<wirelab::AnomalyEvent>& events, wirelab::AnomalyType type)
  {
    return std::any_of(
        events.begin(), events.end(), [type](const wirelab::AnomalyEvent& event) { return event.type == type; });
  }

  TEST(TrafficGeneratorTest, SameSeedProducesSameFrames)
  {
    wirelab::TrafficGeneratorConfig config;
    config.seed = 42;
    config.scenario = wirelab::TrafficScenario::Mixed;
    config.frame_size = 128;

    wirelab::DeterministicTrafficGenerator first(config);
    wirelab::DeterministicTrafficGenerator second(config);

    EXPECT_EQ(first.generate(32), second.generate(32));
  }

  TEST(TrafficGeneratorTest, CounterBasedFramesMatchTheSequentialGenerator)
  {
    wirelab::TrafficGeneratorConfig config;
    config.seed = 42;
    config.scenario = wirelab::TrafficScenario::Mixed;
    config.frame_size = 96;

    wirelab::DeterministicTrafficGenerator generator(config);
    const auto frames = generator.generate(16);

    for (uint64_t sequence = 0; sequence < frames.size(); ++sequence)
    {
      EXPECT_EQ(wirelab::traffic_frame(config, sequence), frames[sequence]) << "sequence " << sequence;
    }
  }

  TEST(TrafficGeneratorTest, WriteTrafficFrameMatchesTrafficFrame)
  {
    wirelab::TrafficGeneratorConfig config;
    config.seed = 11;
    config.scenario = wirelab::TrafficScenario::PortScan;
    config.frame_size = 74;

    std::vector<uint8_t> buffer(config.frame_size);
    for (uint64_t sequence = 0; sequence < 8; ++sequence)
    {
      const auto expected = wirelab::traffic_frame(config, sequence);
      const size_t written = wirelab::write_traffic_frame(config, sequence, buffer.data());
      ASSERT_EQ(written, config.frame_size);
      EXPECT_EQ(buffer, expected) << "sequence " << sequence;
    }
  }

  TEST(TrafficGeneratorTest, GeneratesRequestedBroadcastFrames)
  {
    wirelab::TrafficGeneratorConfig config;
    config.scenario = wirelab::TrafficScenario::Broadcast;
    config.frame_size = 64;

    wirelab::DeterministicTrafficGenerator generator(config);
    const auto bytes = generator.next_frame();
    const auto frame = wirelab::EthernetFrame::parse(bytes);
    const wirelab::PacketView packet{ bytes.data(), bytes.size() };
    wirelab::CpuPacketAnalyzer analyzer;
    const auto analysis = analyzer.analyze(&packet, 1);

    EXPECT_TRUE(frame.is_broadcast());
    EXPECT_EQ(frame.size(), config.frame_size);
    EXPECT_EQ(frame.ethertype(), wirelab::EtherType::IPv4);
    ASSERT_EQ(analysis.packets.size(), 1U);
    EXPECT_EQ(analysis.malformed_packets, 0U);
    EXPECT_EQ(analysis.packets[0].source_ipv4, 0x0a000000U);
    EXPECT_EQ(analysis.packets[0].destination_ipv4, 0x0a000001U);
  }

  TEST(TrafficGeneratorTest, MixedCyclesOnlyTheThreeBenignScenarios)
  {
    wirelab::TrafficGeneratorConfig config;
    config.scenario = wirelab::TrafficScenario::Mixed;
    config.frame_size = 128;

    wirelab::DeterministicTrafficGenerator generator(config);
    const auto frames = generator.generate(12);

    for (size_t index = 0; index < frames.size(); ++index)
    {
      const auto frame = wirelab::EthernetFrame::parse(frames[index]);
      const auto& destination = frame.dst_mac().bytes();
      switch (index % 3)
      {
        case 0:
          EXPECT_EQ(destination[0], 0x02) << "frame " << index;
          EXPECT_EQ(destination[1], 0x57) << "frame " << index;
          break;
        case 1: EXPECT_TRUE(frame.dst_mac().is_broadcast()) << "frame " << index; break;
        default:
          EXPECT_EQ(destination[0], 0x02) << "frame " << index;
          EXPECT_EQ(destination[1], 0xfe) << "frame " << index;
          break;
      }
      // No attack scenario leaks into the rotation: none of the three carries a
      // transport header.
      EXPECT_EQ(frame.payload()[9], 0) << "frame " << index;
    }
  }

  TEST(TrafficGeneratorTest, UdpFloodScenarioTripsTheUdpFloodDetector)
  {
    const auto events = scenario_anomalies(wirelab::TrafficScenario::UdpFlood);

    ASSERT_TRUE(contains(events, wirelab::AnomalyType::UdpFlood));
    const auto flood = std::find_if(
        events.begin(),
        events.end(),
        [](const wirelab::AnomalyEvent& event) { return event.type == wirelab::AnomalyType::UdpFlood; });
    EXPECT_EQ(flood->source_ipv4, 0x0a000000U);
    EXPECT_EQ(flood->observed_packets, SCENARIO_FRAME_COUNT);
    EXPECT_FALSE(contains(events, wirelab::AnomalyType::PortScan));
  }

  TEST(TrafficGeneratorTest, PortScanScenarioTripsThePortScanDetector)
  {
    const auto events = scenario_anomalies(wirelab::TrafficScenario::PortScan);

    ASSERT_TRUE(contains(events, wirelab::AnomalyType::PortScan));
    const auto scan = std::find_if(
        events.begin(),
        events.end(),
        [](const wirelab::AnomalyEvent& event) { return event.type == wirelab::AnomalyType::PortScan; });
    EXPECT_EQ(scan->source_ipv4, 0x0a000000U);
    EXPECT_EQ(scan->observed_distinct_destinations, SCENARIO_FRAME_COUNT);
  }

  TEST(TrafficGeneratorTest, BroadcastStormScenarioTripsTheBroadcastStormDetector)
  {
    const auto events = scenario_anomalies(wirelab::TrafficScenario::BroadcastStorm);

    ASSERT_TRUE(contains(events, wirelab::AnomalyType::BroadcastStorm));
    uint64_t stormed_packets = 0;
    for (const auto& event : events)
    {
      if (event.type == wirelab::AnomalyType::BroadcastStorm)
      {
        stormed_packets += event.observed_packets;
      }
    }
    EXPECT_EQ(stormed_packets, SCENARIO_FRAME_COUNT);
  }

  TEST(TrafficGeneratorTest, EveryScenarioNameRoundTrips)
  {
    for (const auto scenario : ALL_SCENARIOS)
    {
      const char* const name = wirelab::to_string(scenario);
      const auto parsed = wirelab::traffic_scenario_from_string(name);
      ASSERT_TRUE(parsed.has_value()) << name;
      EXPECT_EQ(*parsed, scenario) << name;
    }
  }

  TEST(TrafficGeneratorTest, RejectsIncompleteEthernetFrame)
  {
    wirelab::TrafficGeneratorConfig config;
    config.frame_size = wirelab::ETHERNET_HEADER_SIZE - 1;

    EXPECT_THROW(wirelab::DeterministicTrafficGenerator generator(config), std::invalid_argument);
  }
}  // namespace
