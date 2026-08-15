#include <gtest/gtest.h>

#include <stdexcept>

#include "project/ethernet_frame.hpp"
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
    const auto frame = project::EthernetFrame::parse(generator.next_frame());

    EXPECT_TRUE(frame.is_broadcast());
    EXPECT_EQ(frame.size(), config.frame_size);
    EXPECT_EQ(frame.ethertype(), project::EtherType::IPv4);
  }

  TEST(TrafficGeneratorTest, RejectsIncompleteEthernetFrame)
  {
    project::TrafficGeneratorConfig config;
    config.frame_size = project::ETHERNET_HEADER_SIZE - 1;

    EXPECT_THROW(project::DeterministicTrafficGenerator generator(config), std::invalid_argument);
  }
}
