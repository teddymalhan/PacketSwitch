#include <gtest/gtest.h>

#include "project/control_service.hpp"

namespace
{
  project::VSwitch make_switch()
  {
    auto created = project::VSwitch::create(0, project::VSwitchLogLevel::Frame);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  TEST(ControlServiceTest, ReturnsAcknowledgementAndMetricsSnapshot)
  {
    auto vswitch = make_switch();
    project::ControlService service(vswitch, 3);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":3})");

    EXPECT_TRUE(result.reply.accepted);
    EXPECT_EQ(result.reply.request_id, "state-1");
    EXPECT_EQ(result.reply.operation_id, "switch-state-1");
    ASSERT_TRUE(result.metrics_event.has_value());
    EXPECT_EQ(result.metrics_event->event_sequence, 1U);
    EXPECT_EQ(result.metrics_event->topology_revision, 3U);
    EXPECT_EQ(result.metrics_event->metrics.received_packets, 0U);
  }

  TEST(ControlServiceTest, RejectsInvalidAndStaleRequests)
  {
    auto vswitch = make_switch();
    project::ControlService service(vswitch, 3);

    const auto malformed = service.dispatch("not json");
    EXPECT_FALSE(malformed.reply.accepted);
    EXPECT_EQ(malformed.reply.error, "malformed JSON");
    EXPECT_FALSE(malformed.metrics_event.has_value());

    const auto stale = service.dispatch(
        R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":2})");
    EXPECT_FALSE(stale.reply.accepted);
    EXPECT_EQ(stale.reply.request_id, "state-1");
    EXPECT_EQ(stale.reply.error, "stale topology revision");
  }

  TEST(ControlServiceTest, RejectsCommandsWithoutAnImplementation)
  {
    auto vswitch = make_switch();
    project::ControlService service(vswitch);

    const auto result = service.dispatch(
        R"({"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":1,"duration_seconds":1,"seed":42}})");

    EXPECT_FALSE(result.reply.accepted);
    EXPECT_EQ(result.reply.error, "command is not implemented");
    EXPECT_FALSE(result.metrics_event.has_value());
  }
}
