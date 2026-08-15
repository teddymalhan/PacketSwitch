#include <gtest/gtest.h>

#include <vector>

#include "wirelab/policy_engine.hpp"

namespace
{
  wirelab::AnomalyEvent anomaly(wirelab::AnomalyType type, uint32_t ingress_port = 7)
  {
    wirelab::AnomalyEvent event;
    event.type = type;
    event.ingress_port = ingress_port;
    event.observed_packets = 101;
    event.threshold = 100;
    return event;
  }

  TEST(PolicyEngineTest, AppliesFirstEnabledMatchingRuleAndTracksHits)
  {
    wirelab::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "observe-storm", wirelab::AnomalyType::BroadcastStorm,
                                  wirelab::PolicyAction::Mirror }));
    ASSERT_TRUE(engine.add_rule({ "contain-storm", wirelab::AnomalyType::BroadcastStorm,
                                  wirelab::PolicyAction::Quarantine }));
    ASSERT_TRUE(engine.add_rule({ "limit-udp", wirelab::AnomalyType::UdpFlood,
                                  wirelab::PolicyAction::RateLimit, true, 50'000 }));

    const auto decisions = engine.evaluate(
        { anomaly(wirelab::AnomalyType::BroadcastStorm), anomaly(wirelab::AnomalyType::UdpFlood) });

    ASSERT_EQ(decisions.size(), 2U);
    EXPECT_EQ(decisions[0].rule_name, "observe-storm");
    EXPECT_EQ(decisions[0].action, wirelab::PolicyAction::Mirror);
    EXPECT_EQ(decisions[0].anomaly.ingress_port, 7U);
    EXPECT_EQ(decisions[0].hit_count, 1U);
    EXPECT_EQ(decisions[1].rule_name, "limit-udp");
    EXPECT_EQ(decisions[1].rate_limit_packets_per_second, 50'000U);
    EXPECT_EQ(engine.hit_count("observe-storm"), 1U);

    const auto repeated = engine.evaluate({ anomaly(wirelab::AnomalyType::BroadcastStorm) });
    ASSERT_EQ(repeated.size(), 1U);
    EXPECT_EQ(repeated.front().hit_count, 2U);
  }

  TEST(PolicyEngineTest, SupportsRuleLifecycleAndReset)
  {
    wirelab::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "drop-unknown", wirelab::AnomalyType::UnknownUnicastFlood,
                                  wirelab::PolicyAction::Drop }));

    ASSERT_TRUE(engine.set_enabled("drop-unknown", false));
    EXPECT_TRUE(engine.evaluate({ anomaly(wirelab::AnomalyType::UnknownUnicastFlood) }).empty());
    ASSERT_TRUE(engine.set_enabled("drop-unknown", true));
    EXPECT_EQ(engine.evaluate({ anomaly(wirelab::AnomalyType::UnknownUnicastFlood) }).size(), 1U);
    EXPECT_EQ(engine.hit_count("drop-unknown"), 1U);

    engine.reset();
    EXPECT_EQ(engine.hit_count("drop-unknown"), 0U);
    ASSERT_TRUE(engine.remove_rule("drop-unknown"));
    EXPECT_FALSE(engine.remove_rule("drop-unknown"));
    EXPECT_FALSE(engine.set_enabled("drop-unknown", true));
  }

  TEST(PolicyEngineTest, RejectsInvalidConfigurationsWithoutChangingRules)
  {
    wirelab::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "allow-mac-flap", wirelab::AnomalyType::MacFlap, wirelab::PolicyAction::Allow }));

    const auto empty_name = engine.add_rule({ "", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::Drop });
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.error(), wirelab::PolicyConfigurationError::EmptyName);

    const auto duplicate = engine.add_rule(
        { "allow-mac-flap", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::Drop });
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error(), wirelab::PolicyConfigurationError::DuplicateName);

    const auto missing_rate = engine.add_rule(
        { "limit-udp", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::RateLimit });
    ASSERT_FALSE(missing_rate);
    EXPECT_EQ(missing_rate.error(), wirelab::PolicyConfigurationError::MissingRateLimit);

    const auto replacement = engine.set_rules(
        { { "one", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Drop },
          { "one", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::Drop } });
    ASSERT_FALSE(replacement);
    EXPECT_EQ(replacement.error(), wirelab::PolicyConfigurationError::DuplicateName);
    ASSERT_EQ(engine.rules().size(), 1U);
    EXPECT_EQ(engine.rules().front().name, "allow-mac-flap");
  }
}
