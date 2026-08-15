#include <gtest/gtest.h>

#include <stdexcept>

#include "wirelab/ethernet_frame.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/traffic_generator.hpp"

namespace
{
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

  TEST(TrafficGeneratorTest, RejectsIncompleteEthernetFrame)
  {
    wirelab::TrafficGeneratorConfig config;
    config.frame_size = wirelab::ETHERNET_HEADER_SIZE - 1;

    EXPECT_THROW(wirelab::DeterministicTrafficGenerator generator(config), std::invalid_argument);
  }
}
