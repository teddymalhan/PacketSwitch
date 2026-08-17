#include "wirelab/analysis_pipeline.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>

namespace
{
  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "pipeline-lab";
    configuration.nodes = { { "client-a", wirelab::TopologyNodeType::Host },
                            { "client-b", wirelab::TopologyNodeType::Host },
                            { "core-switch", wirelab::TopologyNodeType::Switch } };
    configuration.links = { { "client-a", "core-switch", std::chrono::milliseconds(1) },
                            { "client-b", "core-switch", std::chrono::milliseconds(1) } };
    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  wirelab::AnalysisBatch broadcast_batch(uint32_t ingress_port, size_t packets)
  {
    wirelab::AnalysisBatch batch;
    wirelab::PacketAnalysis packet;
    packet.source_mac = wirelab::MacAddress::from_string("00:11:22:33:44:55");
    packet.destination_mac = wirelab::MacAddress::broadcast();
    packet.frame_length = 64;
    packet.ingress_port = ingress_port;
    packet.validity = wirelab::PacketValidity::Valid;
    packet.classification = wirelab::PacketClassification::Broadcast;
    batch.packets.assign(packets, packet);
    batch.received_packets = packets;
    batch.broadcast_packets = packets;
    return batch;
  }

  wirelab::AnomalyDetectorConfig sensitive_config()
  {
    wirelab::AnomalyDetectorConfig config;
    config.window_duration_ns = 1'000'000'000;
    config.broadcast_packets_threshold = 1;
    return config;
  }

  TEST(AnalysisPipelineTest, DetectsAndDecidesWithoutATopologyController)
  {
    wirelab::AnalysisPipeline pipeline(sensitive_config());
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    const auto outcome = pipeline.evaluate(broadcast_batch(0, 2), 500, std::chrono::steady_clock::now());

    EXPECT_FALSE(pipeline.enforces());
    ASSERT_EQ(outcome.anomalies.size(), 1U);
    EXPECT_EQ(outcome.anomalies[0].type, wirelab::AnomalyType::BroadcastStorm);
    ASSERT_EQ(outcome.decisions.size(), 1U);
    EXPECT_EQ(outcome.decisions[0].rule_name, "contain-broadcast-storm");
    // Nothing to enforce on: a capture or a detached run must not fabricate a port.
    EXPECT_TRUE(outcome.released.empty());
    EXPECT_TRUE(outcome.enforced.empty());
  }

  TEST(AnalysisPipelineTest, EnforcesOntoTheOffendingPortWhenAttached)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::AnalysisPipeline pipeline(sensitive_config(), controller);
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    const auto outcome = pipeline.evaluate(broadcast_batch(1, 2), 500, std::chrono::steady_clock::now());

    EXPECT_TRUE(pipeline.enforces());
    ASSERT_EQ(outcome.enforced.size(), 1U);
    EXPECT_EQ(outcome.enforced[0].port_id, "client-b");
    EXPECT_EQ(outcome.enforced[0].outcome, wirelab::EnforcementOutcome::Applied);
    const auto fault = controller.port_fault("client-b");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->isolated);
  }

  TEST(AnalysisPipelineTest, ClampsABackwardsBatchClockForward)
  {
    wirelab::AnalysisPipeline pipeline(sensitive_config());

    const auto first = pipeline.evaluate(broadcast_batch(0, 2), 2'000'000'000, std::chrono::steady_clock::now());
    ASSERT_EQ(first.anomalies.size(), 1U);
    EXPECT_EQ(first.timestamp_ns, 2'000'000'000U);

    // An out-of-order record must not rewind the detection window, which would
    // otherwise let already-expired observations count again.
    const auto second = pipeline.evaluate(broadcast_batch(0, 2), 5, std::chrono::steady_clock::now());
    EXPECT_EQ(second.timestamp_ns, 2'000'000'000U);
  }

  TEST(AnalysisPipelineTest, ReleasesALeaseEarlyAndRestoresThePort)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::AnalysisPipeline pipeline(sensitive_config(), controller);
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));
    const auto outcome = pipeline.evaluate(broadcast_batch(1, 2), 500, std::chrono::steady_clock::now());
    ASSERT_EQ(outcome.enforced.size(), 1U);

    EXPECT_TRUE(pipeline.release("client-b"));
    EXPECT_FALSE(pipeline.enforcer().is_enforced("client-b"));
    EXPECT_FALSE(controller.port_fault("client-b").has_value());
    EXPECT_FALSE(pipeline.release("client-b"));
  }

  TEST(AnalysisPipelineTest, ExpiredLeasesAreReleasedBeforeNewDecisionsApply)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcerConfig enforcer_config;
    enforcer_config.quarantine_lease = std::chrono::seconds{ 3 };
    wirelab::AnalysisPipeline pipeline(sensitive_config(), controller, enforcer_config);
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    const auto start = std::chrono::steady_clock::now();
    const auto first = pipeline.evaluate(broadcast_batch(1, 2), 500, start);
    ASSERT_EQ(first.enforced.size(), 1U);

    // A quiet window clears the anomaly so the next storm is a fresh event; the
    // lease is still live, so nothing is released yet.
    const auto quiet = pipeline.evaluate({}, 2'000'000'000, start + std::chrono::milliseconds{ 500 });
    EXPECT_TRUE(quiet.anomalies.empty());
    EXPECT_TRUE(quiet.released.empty());
    EXPECT_TRUE(pipeline.enforcer().is_enforced("client-b"));

    // The storm returns after the lease has expired: the expiry must be reported
    // and the restored baseline re-enforced in the same evaluation.
    const auto second = pipeline.evaluate(broadcast_batch(1, 2), 3'000'000'000, start + std::chrono::seconds{ 4 });
    ASSERT_EQ(second.released.size(), 1U);
    EXPECT_EQ(second.released[0].port_id, "client-b");
    EXPECT_EQ(second.released[0].outcome, wirelab::EnforcementOutcome::Released);
    ASSERT_EQ(second.enforced.size(), 1U);
    EXPECT_EQ(second.enforced[0].outcome, wirelab::EnforcementOutcome::Applied);
  }

  TEST(AnalysisPipelineTest, ResetClearsDetectionStateHitCountsAndTheBatchClock)
  {
    wirelab::AnalysisPipeline pipeline(sensitive_config());
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));
    const auto first = pipeline.evaluate(broadcast_batch(0, 2), 2'000'000'000, std::chrono::steady_clock::now());
    ASSERT_EQ(first.decisions.size(), 1U);
    EXPECT_EQ(pipeline.policies().hit_count("contain-broadcast-storm"), 1U);

    pipeline.reset();

    EXPECT_EQ(pipeline.policies().hit_count("contain-broadcast-storm"), 0U);
    // Rules survive a reset; only the observed state does.
    EXPECT_EQ(pipeline.policies().rules().size(), 1U);
    const auto after = pipeline.evaluate(broadcast_batch(0, 2), 5, std::chrono::steady_clock::now());
    EXPECT_EQ(after.timestamp_ns, 5U);
    ASSERT_EQ(after.anomalies.size(), 1U);
  }
}  // namespace
