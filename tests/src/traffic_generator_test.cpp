#include <gtest/gtest.h>

#include <stdexcept>

#include "project/ethernet_frame.hpp"
#include "project/packet_analyzer.hpp"
#include "project/traffic_generator.hpp"

namespace
{
  TEST(TrafficGeneratorTest, SameSeedProducesSameFrames)
  {
    project::TrafficGeneratorConfig config;
    config.seed = 42;
    config.scenario = project::TrafficScenario::Mixed;
    config.frame_size = 128;

    project::DeterministicTrafficGenerator first(config);
    project::DeterministicTrafficGenerator second(config);

    EXPECT_EQ(first.generate(32), second.generate(32));
  }

  TEST(TrafficGeneratorTest, GeneratesRequestedBroadcastFrames)
  {
    project::TrafficGeneratorConfig config;
    config.scenario = project::TrafficScenario::Broadcast;
    config.frame_size = 64;

    project::DeterministicTrafficGenerator generator(config);
    const auto bytes = generator.next_frame();
    const auto frame = project::EthernetFrame::parse(bytes);
    const project::PacketView packet{ bytes.data(), bytes.size() };
    project::CpuPacketAnalyzer analyzer;
    const auto analysis = analyzer.analyze(&packet, 1);

    EXPECT_TRUE(frame.is_broadcast());
    EXPECT_EQ(frame.size(), config.frame_size);
    EXPECT_EQ(frame.ethertype(), project::EtherType::IPv4);
    ASSERT_EQ(analysis.packets.size(), 1U);
    EXPECT_EQ(analysis.malformed_packets, 0U);
    EXPECT_EQ(analysis.packets[0].source_ipv4, 0x0a000000U);
    EXPECT_EQ(analysis.packets[0].destination_ipv4, 0x0a000001U);
  }

  TEST(TrafficGeneratorTest, RejectsIncompleteEthernetFrame)
  {
    project::TrafficGeneratorConfig config;
    config.frame_size = project::ETHERNET_HEADER_SIZE - 1;

    EXPECT_THROW(project::DeterministicTrafficGenerator generator(config), std::invalid_argument);
  }
}
