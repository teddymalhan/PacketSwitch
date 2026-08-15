#include <chrono>

#include <gtest/gtest.h>

#include "wirelab/fault_engine.hpp"

namespace
{
  using Clock = std::chrono::steady_clock;

  TEST(FaultEngineTest, PassesUnconfiguredTargetsAtArrival)
  {
    wirelab::FaultEngine engine(42);
    const auto arrival = Clock::time_point(std::chrono::seconds(5));

    const auto decision = engine.evaluate("client-a", 64, arrival);

    EXPECT_FALSE(decision.dropped);
    EXPECT_EQ(decision.delivery_count, 1);
    EXPECT_EQ(decision.delivery_times[0], arrival);
  }

  TEST(FaultEngineTest, RejectsInvalidConfiguration)
  {
    wirelab::FaultEngine engine;

    EXPECT_EQ(engine.set_fault("", {}).error(), wirelab::FaultConfigurationError::MissingTarget);
    EXPECT_EQ(
        engine.set_fault("client-a", { std::chrono::nanoseconds(-1) }).error(),
        wirelab::FaultConfigurationError::NegativeLatency);
    EXPECT_EQ(
        engine.set_fault("client-a", { {}, {}, 10001 }).error(),
        wirelab::FaultConfigurationError::InvalidLossPercentage);
    EXPECT_EQ(
        engine.set_fault("client-a", { {}, {}, 0, 10001 }).error(),
        wirelab::FaultConfigurationError::InvalidDuplicationPercentage);
  }

  TEST(FaultEngineTest, AppliesLatencyAndBandwidthInArrivalOrder)
  {
    wirelab::FaultEngine engine(42);
    wirelab::FaultConfiguration configuration;
    configuration.latency = std::chrono::milliseconds(2);
    configuration.bandwidth_bits_per_second = 8000;
    ASSERT_TRUE(engine.set_fault("client-a", configuration));

    const auto arrival = Clock::time_point{};
    const auto first = engine.evaluate("client-a", 100, arrival);
    const auto second = engine.evaluate("client-a", 100, arrival);

    ASSERT_FALSE(first.dropped);
    ASSERT_FALSE(second.dropped);
    EXPECT_EQ(first.delivery_count, 1);
    EXPECT_EQ(second.delivery_count, 1);
    EXPECT_EQ(first.delivery_times[0], arrival + std::chrono::milliseconds(2));
    EXPECT_EQ(second.delivery_times[0], arrival + std::chrono::milliseconds(102));
  }

  TEST(FaultEngineTest, DropsBlackholedAndIsolatedTargets)
  {
    wirelab::FaultEngine engine;
    wirelab::FaultConfiguration blackhole;
    blackhole.blackhole = true;
    ASSERT_TRUE(engine.set_fault("blackhole", blackhole));
    wirelab::FaultConfiguration isolated;
    isolated.isolated = true;
    ASSERT_TRUE(engine.set_fault("isolated", isolated));

    EXPECT_TRUE(engine.evaluate("blackhole", 64, Clock::time_point{}).dropped);
    EXPECT_TRUE(engine.evaluate("isolated", 64, Clock::time_point{}).dropped);
  }

  TEST(FaultEngineTest, AppliesCertainLossAndDuplication)
  {
    wirelab::FaultEngine engine;
    wirelab::FaultConfiguration loss;
    loss.loss_basis_points = 10000;
    ASSERT_TRUE(engine.set_fault("loss", loss));
    wirelab::FaultConfiguration duplicate;
    duplicate.duplication_basis_points = 10000;
    ASSERT_TRUE(engine.set_fault("duplicate", duplicate));

    EXPECT_TRUE(engine.evaluate("loss", 64, Clock::time_point{}).dropped);
    const auto duplicated = engine.evaluate("duplicate", 64, Clock::time_point{});
    EXPECT_FALSE(duplicated.dropped);
    EXPECT_EQ(duplicated.delivery_count, 2);
  }

  TEST(FaultEngineTest, ProducesRepeatableDecisionsForSameSeedAndConfiguration)
  {
    wirelab::FaultConfiguration configuration;
    configuration.jitter = std::chrono::microseconds(100);
    configuration.loss_basis_points = 2500;
    configuration.duplication_basis_points = 5000;
    wirelab::FaultEngine first(99);
    wirelab::FaultEngine second(99);
    ASSERT_TRUE(first.set_fault("client-a", configuration));
    ASSERT_TRUE(second.set_fault("client-a", configuration));

    for (int index = 0; index < 20; ++index)
    {
      const auto arrival = Clock::time_point(std::chrono::milliseconds(index));
      const auto first_decision = first.evaluate("client-a", 64, arrival);
      const auto second_decision = second.evaluate("client-a", 64, arrival);
      EXPECT_EQ(first_decision.dropped, second_decision.dropped);
      EXPECT_EQ(first_decision.delivery_count, second_decision.delivery_count);
      EXPECT_EQ(first_decision.delivery_times, second_decision.delivery_times);
    }
  }

  TEST(FaultEngineTest, ClearsConfiguredFault)
  {
    wirelab::FaultEngine engine;
    ASSERT_TRUE(engine.set_fault("client-a", {}));

    EXPECT_TRUE(engine.has_fault("client-a"));
    EXPECT_TRUE(engine.clear_fault("client-a"));
    EXPECT_FALSE(engine.has_fault("client-a"));
    EXPECT_FALSE(engine.clear_fault("client-a"));
  }

  TEST(FaultEngineTest, ReportsActiveFaultsInStableTargetOrder)
  {
    wirelab::FaultEngine engine;
    wirelab::FaultConfiguration first_configuration;
    first_configuration.blackhole = true;
    wirelab::FaultConfiguration second_configuration;
    second_configuration.loss_basis_points = 2500;

    ASSERT_TRUE(engine.set_fault("port:zeta", first_configuration).has_value());
    ASSERT_TRUE(engine.set_fault("port:alpha", second_configuration).has_value());

    const auto faults = engine.active_faults();
    ASSERT_EQ(faults.size(), 2U);
    EXPECT_EQ(faults[0].target, "port:alpha");
    EXPECT_EQ(faults[0].configuration.loss_basis_points, 2500U);
    EXPECT_EQ(faults[1].target, "port:zeta");
    EXPECT_TRUE(faults[1].configuration.blackhole);
  }
}
