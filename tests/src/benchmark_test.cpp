#include <gtest/gtest.h>

#include <cstdint>
#include <tuple>

#include "wirelab/benchmark.hpp"

namespace
{
  // The counters a run must reproduce regardless of how it was driven; timing and
  // throughput are wall-clock dependent and deliberately excluded.
  using BenchmarkCounters = std::
      tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>;

  [[nodiscard]] BenchmarkCounters counters_of(const wirelab::BenchmarkResult& result)
  {
    return { result.total_packets,       result.completed_packets,        result.received_packets,
             result.received_bytes,      result.malformed_packets,        result.broadcast_packets,
             result.unknown_unicast_packets, result.known_unicast_packets };
  }

  [[nodiscard]] wirelab::BenchmarkConfig mixed_traffic_config()
  {
    wirelab::BenchmarkConfig config;
    config.traffic.scenario = wirelab::TrafficScenario::Mixed;
    config.traffic.seed = 42;
    config.traffic.frame_size = 64;
    config.packet_count = 40;
    config.batch_size = 8;
    config.backend = "cpu";
    return config;
  }

  TEST(BenchmarkTest, SameSeedProducesSameCounters)
  {
    auto first = wirelab::BenchmarkRun::create(mixed_traffic_config());
    auto second = wirelab::BenchmarkRun::create(mixed_traffic_config());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first->advance(first->total_packets()), 40U);
    EXPECT_EQ(second->advance(second->total_packets()), 40U);

    EXPECT_EQ(counters_of(first->result()), counters_of(second->result()));
  }

  TEST(BenchmarkTest, SlicedRunMatchesSingleAdvance)
  {
    auto sliced = wirelab::BenchmarkRun::create(mixed_traffic_config());
    auto whole = wirelab::BenchmarkRun::create(mixed_traffic_config());
    ASSERT_TRUE(sliced.has_value());
    ASSERT_TRUE(whole.has_value());

    size_t slices = 0;
    while (!sliced->finished())
    {
      // A slice budget below batch_size must still make progress, by whole batches.
      EXPECT_EQ(sliced->advance(7), 8U);
      ++slices;
    }
    EXPECT_EQ(slices, 5U);
    EXPECT_EQ(sliced->advance(7), 0U);

    EXPECT_EQ(whole->advance(1000), 40U);
    EXPECT_TRUE(whole->finished());

    EXPECT_EQ(counters_of(sliced->result()), counters_of(whole->result()));
  }

  TEST(BenchmarkTest, RejectsInvalidConfiguration)
  {
    auto no_packets = mixed_traffic_config();
    no_packets.packet_count = 0;
    auto no_batch = mixed_traffic_config();
    no_batch.batch_size = 0;
    auto short_frame = mixed_traffic_config();
    short_frame.traffic.frame_size = 4;
    auto jumbo_frame = mixed_traffic_config();
    jumbo_frame.traffic.frame_size = 9001;

    const auto rejected_packets = wirelab::BenchmarkRun::create(no_packets);
    const auto rejected_batch = wirelab::BenchmarkRun::create(no_batch);
    const auto rejected_frame = wirelab::BenchmarkRun::create(short_frame);
    const auto rejected_jumbo = wirelab::BenchmarkRun::create(jumbo_frame);

    ASSERT_FALSE(rejected_packets.has_value());
    ASSERT_FALSE(rejected_batch.has_value());
    ASSERT_FALSE(rejected_frame.has_value());
    ASSERT_FALSE(rejected_jumbo.has_value());
    EXPECT_EQ(rejected_packets.error(), wirelab::BenchmarkError::InvalidConfiguration);
    EXPECT_EQ(rejected_batch.error(), wirelab::BenchmarkError::InvalidConfiguration);
    EXPECT_EQ(rejected_frame.error(), wirelab::BenchmarkError::InvalidConfiguration);
    EXPECT_EQ(rejected_jumbo.error(), wirelab::BenchmarkError::InvalidConfiguration);
  }

  TEST(BenchmarkTest, RejectsUnknownBackend)
  {
    auto config = mixed_traffic_config();
    config.backend = "quantum";

    const auto rejected = wirelab::BenchmarkRun::create(config);

    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), wirelab::BenchmarkError::UnknownBackend);
  }

  TEST(BenchmarkTest, FinishedRunReportsCompleteResult)
  {
    auto run = wirelab::BenchmarkRun::create(mixed_traffic_config());
    ASSERT_TRUE(run.has_value());

    while (!run->finished())
    {
      EXPECT_GT(run->advance(16), 0U);
    }
    const wirelab::BenchmarkResult result = run->result();

    EXPECT_EQ(result.completed_packets, result.total_packets);
    EXPECT_EQ(result.completed_packets, run->completed_packets());
    EXPECT_EQ(result.received_packets, 40U);
    EXPECT_GT(result.received_bytes, 0U);
    EXPECT_EQ(result.backend, "cpu");
    EXPECT_EQ(result.scenario, "mixed-traffic");
    EXPECT_EQ(result.seed, 42U);
    EXPECT_EQ(result.frame_size, 64U);
    EXPECT_EQ(result.batch_size, 8U);
    EXPECT_GE(result.batch_analysis_latency_p99_ns, result.batch_analysis_latency_p50_ns);
    EXPECT_EQ(result.timing.kernel_ns, 0U);
  }

  TEST(BenchmarkTest, ParsesScenarioNames)
  {
    const auto known = wirelab::traffic_scenario_from_string("known-unicast");
    const auto unknown = wirelab::traffic_scenario_from_string("carrier-pigeon");

    ASSERT_TRUE(known.has_value());
    EXPECT_EQ(*known, wirelab::TrafficScenario::KnownUnicast);
    EXPECT_STREQ(wirelab::to_string(wirelab::TrafficScenario::UnknownUnicast), "unknown-unicast");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error(), wirelab::BenchmarkError::UnknownScenario);
  }
}
