#include <gtest/gtest.h>

#include <vector>

#include "project/policy_engine.hpp"

namespace
{
  project::AnomalyEvent anomaly(project::AnomalyType type, uint32_t ingress_port = 7)
  {
    project::AnomalyEvent event;
    event.type = type;
    event.ingress_port = ingress_port;
    event.observed_packets = 101;
    event.threshold = 100;
    return event;
  }

  TEST(PolicyEngineTest, AppliesFirstEnabledMatchingRuleAndTracksHits)
  {
    project::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "observe-storm", project::AnomalyType::BroadcastStorm,
                                  project::PolicyAction::Mirror }));
    ASSERT_TRUE(engine.add_rule({ "contain-storm", project::AnomalyType::BroadcastStorm,
                                  project::PolicyAction::Quarantine }));
    ASSERT_TRUE(engine.add_rule({ "limit-udp", project::AnomalyType::UdpFlood,
                                  project::PolicyAction::RateLimit, true, 50'000 }));

    const auto decisions = engine.evaluate(
        { anomaly(project::AnomalyType::BroadcastStorm), anomaly(project::AnomalyType::UdpFlood) });

    ASSERT_EQ(decisions.size(), 2U);
    EXPECT_EQ(decisions[0].rule_name, "observe-storm");
    EXPECT_EQ(decisions[0].action, project::PolicyAction::Mirror);
    EXPECT_EQ(decisions[0].anomaly.ingress_port, 7U);
    EXPECT_EQ(decisions[0].hit_count, 1U);
    EXPECT_EQ(decisions[1].rule_name, "limit-udp");
    EXPECT_EQ(decisions[1].rate_limit_packets_per_second, 50'000U);
    EXPECT_EQ(engine.hit_count("observe-storm"), 1U);

    const auto repeated = engine.evaluate({ anomaly(project::AnomalyType::BroadcastStorm) });
    ASSERT_EQ(repeated.size(), 1U);
    EXPECT_EQ(repeated.front().hit_count, 2U);
  }

  TEST(PolicyEngineTest, SupportsRuleLifecycleAndReset)
  {
    project::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "drop-unknown", project::AnomalyType::UnknownUnicastFlood,
                                  project::PolicyAction::Drop }));

    ASSERT_TRUE(engine.set_enabled("drop-unknown", false));
    EXPECT_TRUE(engine.evaluate({ anomaly(project::AnomalyType::UnknownUnicastFlood) }).empty());
    ASSERT_TRUE(engine.set_enabled("drop-unknown", true));
    EXPECT_EQ(engine.evaluate({ anomaly(project::AnomalyType::UnknownUnicastFlood) }).size(), 1U);
    EXPECT_EQ(engine.hit_count("drop-unknown"), 1U);

    engine.reset();
    EXPECT_EQ(engine.hit_count("drop-unknown"), 0U);
    ASSERT_TRUE(engine.remove_rule("drop-unknown"));
    EXPECT_FALSE(engine.remove_rule("drop-unknown"));
    EXPECT_FALSE(engine.set_enabled("drop-unknown", true));
  }

  TEST(PolicyEngineTest, RejectsInvalidConfigurationsWithoutChangingRules)
  {
    project::PolicyEngine engine;
    ASSERT_TRUE(engine.add_rule({ "allow-mac-flap", project::AnomalyType::MacFlap, project::PolicyAction::Allow }));

    const auto empty_name = engine.add_rule({ "", project::AnomalyType::UdpFlood, project::PolicyAction::Drop });
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.error(), project::PolicyConfigurationError::EmptyName);

    const auto duplicate = engine.add_rule(
        { "allow-mac-flap", project::AnomalyType::UdpFlood, project::PolicyAction::Drop });
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error(), project::PolicyConfigurationError::DuplicateName);

    const auto missing_rate = engine.add_rule(
        { "limit-udp", project::AnomalyType::UdpFlood, project::PolicyAction::RateLimit });
    ASSERT_FALSE(missing_rate);
    EXPECT_EQ(missing_rate.error(), project::PolicyConfigurationError::MissingRateLimit);

    const auto replacement = engine.set_rules(
        { { "one", project::AnomalyType::BroadcastStorm, project::PolicyAction::Drop },
          { "one", project::AnomalyType::UdpFlood, project::PolicyAction::Drop } });
    ASSERT_FALSE(replacement);
    EXPECT_EQ(replacement.error(), project::PolicyConfigurationError::DuplicateName);
    ASSERT_EQ(engine.rules().size(), 1U);
    EXPECT_EQ(engine.rules().front().name, "allow-mac-flap");
  }
}
