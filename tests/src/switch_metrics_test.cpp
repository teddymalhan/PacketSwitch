#include <gtest/gtest.h>

#include "project/switch_metrics.hpp"

namespace
{
  TEST(SwitchMetricsTest, SnapshotReportsAllCounterClasses)
  {
    project::SwitchMetrics metrics;

    metrics.record_received(64);
    metrics.record_received(128);
    metrics.record_forwarded(64);
    metrics.record_broadcast();
    metrics.record_unknown_unicast();
    metrics.record_drop();
    metrics.record_malformed();
    metrics.record_mac_learned();

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.received_packets, 2U);
    EXPECT_EQ(snapshot.received_bytes, 192U);
    EXPECT_EQ(snapshot.forwarded_packets, 1U);
    EXPECT_EQ(snapshot.forwarded_bytes, 64U);
    EXPECT_EQ(snapshot.broadcast_packets, 1U);
    EXPECT_EQ(snapshot.unknown_unicast_packets, 1U);
    EXPECT_EQ(snapshot.dropped_packets, 1U);
    EXPECT_EQ(snapshot.malformed_packets, 1U);
    EXPECT_EQ(snapshot.learned_macs, 1U);
  }

  TEST(SwitchMetricsTest, MovePreservesCounters)
  {
    project::SwitchMetrics original;
    original.record_received(128);
    original.record_forwarded(128);

    project::SwitchMetrics moved(std::move(original));
    const auto snapshot = moved.snapshot();

    EXPECT_EQ(snapshot.received_packets, 1U);
    EXPECT_EQ(snapshot.received_bytes, 128U);
    EXPECT_EQ(snapshot.forwarded_packets, 1U);
    EXPECT_EQ(snapshot.forwarded_bytes, 128U);
  }
}
