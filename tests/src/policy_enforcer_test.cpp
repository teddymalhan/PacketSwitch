#include "wirelab/policy_enforcer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

namespace
{
  using namespace std::chrono_literals;

  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "security-lab";
    configuration.nodes = {
      { "client-a", wirelab::TopologyNodeType::Host },
      { "client-b", wirelab::TopologyNodeType::Host },
      { "core-switch", wirelab::TopologyNodeType::Switch },
    };
    configuration.links = {
      { "client-a", "core-switch", std::chrono::milliseconds(1) },
      { "client-b", "core-switch", std::chrono::milliseconds(1) },
    };

    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  wirelab::PolicyDecision decision(wirelab::PolicyAction action, uint32_t ingress_port, uint64_t rate_limit_pps = 0)
  {
    wirelab::PolicyDecision result;
    result.rule_name = "rule";
    result.action = action;
    result.rate_limit_packets_per_second = rate_limit_pps;
    result.anomaly.type = wirelab::AnomalyType::BroadcastStorm;
    result.anomaly.ingress_port = ingress_port;
    result.anomaly.observed_packets = 100;
    result.anomaly.observed_bytes = 6400;
    result.anomaly.threshold = 50;
    return result;
  }

  TEST(PolicyEnforcerTest, MapsIngressPortIndexOntoTheTopologyHostOrder)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());

    EXPECT_EQ(controller.port_ids(), (std::vector<std::string>{ "client-a", "client-b" }));
    ASSERT_TRUE(controller.port_id_at(1).has_value());
    EXPECT_EQ(*controller.port_id_at(1), "client-b");
    EXPECT_FALSE(controller.port_id_at(2).has_value());
  }

  TEST(PolicyEnforcerTest, DropBecomesABlackholeFaultOnTheOffendingPort)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;
    const auto now = std::chrono::steady_clock::now();

    const auto actions = enforcer.apply({ decision(wirelab::PolicyAction::Drop, 0) }, controller, now);

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().port_id, "client-a");
    EXPECT_EQ(actions.front().kind, wirelab::EnforcementKind::Blackhole);
    EXPECT_EQ(actions.front().outcome, wirelab::EnforcementOutcome::Applied);

    const auto fault = controller.port_fault("client-a");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->blackhole);
    EXPECT_TRUE(enforcer.is_enforced("client-a"));
    EXPECT_FALSE(controller.port_fault("client-b").has_value());
  }

  TEST(PolicyEnforcerTest, QuarantineIsolatesAndRateLimitDerivesABandwidthCap)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;
    const auto now = std::chrono::steady_clock::now();

    const auto actions = enforcer.apply(
        { decision(wirelab::PolicyAction::Quarantine, 0), decision(wirelab::PolicyAction::RateLimit, 1, 1'000) },
        controller,
        now);

    ASSERT_EQ(actions.size(), 2U);
    EXPECT_TRUE(controller.port_fault("client-a")->isolated);

    // 1000 pps at a 64-byte mean frame (6400 bytes / 100 packets) is 512 kbit/s.
    EXPECT_EQ(actions[1].rate_limit_bits_per_second, 512'000U);
    EXPECT_EQ(controller.port_fault("client-b")->bandwidth_bits_per_second, 512'000U);
  }

  TEST(PolicyEnforcerTest, ObservationalActionsChangeNoForwardingState)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;
    const auto now = std::chrono::steady_clock::now();

    const auto actions = enforcer.apply(
        { decision(wirelab::PolicyAction::AlertOnly, 0),
          decision(wirelab::PolicyAction::Mirror, 0),
          decision(wirelab::PolicyAction::Allow, 0) },
        controller,
        now);

    ASSERT_EQ(actions.size(), 3U);
    for (const auto& action : actions)
    {
      EXPECT_EQ(action.kind, wirelab::EnforcementKind::None);
      EXPECT_EQ(action.outcome, wirelab::EnforcementOutcome::Skipped);
    }
    EXPECT_FALSE(controller.port_fault("client-a").has_value());
    EXPECT_FALSE(enforcer.is_enforced("client-a"));
  }

  TEST(PolicyEnforcerTest, ReportsAnUnresolvableIngressPortWithoutApplyingAFault)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;

    const auto actions =
        enforcer.apply({ decision(wirelab::PolicyAction::Drop, 99) }, controller, std::chrono::steady_clock::now());

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().outcome, wirelab::EnforcementOutcome::UnknownPort);
    EXPECT_TRUE(actions.front().port_id.empty());
    EXPECT_FALSE(enforcer.is_enforced("client-a"));
  }

  TEST(PolicyEnforcerTest, RestoresTheOperatorFaultWhenTheLeaseExpires)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::FaultConfiguration operator_fault;
    operator_fault.latency = 5ms;
    operator_fault.loss_basis_points = 250;
    ASSERT_TRUE(controller.set_port_fault("client-a", operator_fault));

    wirelab::PolicyEnforcerConfig config;
    config.drop_lease = 1s;
    wirelab::PolicyEnforcer enforcer(config);
    const auto now = std::chrono::steady_clock::now();

    ASSERT_EQ(enforcer.apply({ decision(wirelab::PolicyAction::Drop, 0) }, controller, now).size(), 1U);
    EXPECT_TRUE(controller.port_fault("client-a")->blackhole);
    EXPECT_EQ(controller.port_fault("client-a")->loss_basis_points, 250U);

    EXPECT_TRUE(enforcer.release_expired(controller, now + 500ms).empty());

    const auto released = enforcer.release_expired(controller, now + 2s);
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released.front().outcome, wirelab::EnforcementOutcome::Released);

    const auto restored = controller.port_fault("client-a");
    ASSERT_TRUE(restored.has_value());
    EXPECT_FALSE(restored->blackhole);
    EXPECT_EQ(restored->latency, 5ms);
    EXPECT_EQ(restored->loss_basis_points, 250U);
    EXPECT_FALSE(enforcer.is_enforced("client-a"));
  }

  TEST(PolicyEnforcerTest, ClearsThePortWhenNoOperatorFaultPrecededEnforcement)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcerConfig config;
    config.quarantine_lease = 1s;
    wirelab::PolicyEnforcer enforcer(config);
    const auto now = std::chrono::steady_clock::now();

    ASSERT_EQ(enforcer.apply({ decision(wirelab::PolicyAction::Quarantine, 0) }, controller, now).size(), 1U);
    ASSERT_EQ(enforcer.release_expired(controller, now + 2s).size(), 1U);

    EXPECT_FALSE(controller.port_fault("client-a").has_value());
  }

  TEST(PolicyEnforcerTest, AStrongerActionSupersedesAWeakerLeaseButNeverTheReverse)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;
    const auto now = std::chrono::steady_clock::now();

    ASSERT_EQ(enforcer.apply({ decision(wirelab::PolicyAction::RateLimit, 0, 1'000) }, controller, now).size(), 1U);
    EXPECT_EQ(controller.port_fault("client-a")->bandwidth_bits_per_second, 512'000U);

    const auto escalated = enforcer.apply({ decision(wirelab::PolicyAction::Quarantine, 0) }, controller, now);
    ASSERT_EQ(escalated.size(), 1U);
    EXPECT_EQ(escalated.front().kind, wirelab::EnforcementKind::Isolate);
    EXPECT_EQ(escalated.front().outcome, wirelab::EnforcementOutcome::Extended);
    EXPECT_TRUE(controller.port_fault("client-a")->isolated);

    // A weaker rate limit must not downgrade a live quarantine.
    const auto weaker = enforcer.apply({ decision(wirelab::PolicyAction::RateLimit, 0, 1'000) }, controller, now);
    ASSERT_EQ(weaker.size(), 1U);
    EXPECT_EQ(weaker.front().kind, wirelab::EnforcementKind::Isolate);
    EXPECT_TRUE(controller.port_fault("client-a")->isolated);
  }

  TEST(PolicyEnforcerTest, ReleasingByPortRestoresStateAndIsIdempotent)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;

    ASSERT_EQ(
        enforcer.apply({ decision(wirelab::PolicyAction::Drop, 0) }, controller, std::chrono::steady_clock::now()).size(),
        1U);
    ASSERT_EQ(enforcer.active().size(), 1U);

    EXPECT_TRUE(enforcer.release("client-a", controller));
    EXPECT_FALSE(enforcer.release("client-a", controller));
    EXPECT_FALSE(controller.port_fault("client-a").has_value());
    EXPECT_TRUE(enforcer.active().empty());
  }

  TEST(PolicyEnforcerTest, RejectsARateLimitRuleThatCarriesNoRate)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcer enforcer;

    const auto actions =
        enforcer.apply({ decision(wirelab::PolicyAction::RateLimit, 0) }, controller, std::chrono::steady_clock::now());

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().outcome, wirelab::EnforcementOutcome::Rejected);
    EXPECT_FALSE(controller.port_fault("client-a").has_value());
  }
}  // namespace
