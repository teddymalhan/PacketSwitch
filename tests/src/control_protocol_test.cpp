#include <gtest/gtest.h>

#include "project/control_protocol.hpp"

namespace
{
  TEST(ControlProtocolTest, ValidatesVersionedBenchmarkRequest)
  {
    project::ControlRequest request;
    request.request_id = "benchmark-42";
    request.command = project::ControlCommand::StartBenchmark;
    request.topology_revision = 7;
    request.benchmark.scenario = "mixed-traffic";
    request.benchmark.backend = project::AnalyzerBackend::Cpu;
    request.benchmark.batch_size = 2048;
    request.benchmark.duration_seconds = 60;
    request.benchmark.seed = 42;

    EXPECT_TRUE(project::validate(request).has_value());
    EXPECT_EQ(project::to_json(request),
              "{\"api_version\":1,\"request_id\":\"benchmark-42\",\"command\":\"start_benchmark\",\"topology_revision\":7,\"parameters\":{\"scenario\":\"mixed-traffic\",\"backend\":\"cpu\",\"batch_size\":2048,\"duration_seconds\":60,\"seed\":42}}");
  }

  TEST(ControlProtocolTest, RejectsUnsupportedVersionAndInvalidBenchmark)
  {
    project::ControlRequest request;
    request.request_id = "request";
    request.api_version = project::WIRELAB_CONTROL_API_VERSION + 1;
    EXPECT_EQ(project::validate(request).error(), project::ControlValidationError::UnsupportedApiVersion);

    request.api_version = project::WIRELAB_CONTROL_API_VERSION;
    request.command = project::ControlCommand::StartBenchmark;
    request.benchmark.scenario = "mixed-traffic";
    request.benchmark.duration_seconds = 1;
    request.benchmark.batch_size = 0;
    EXPECT_EQ(project::validate(request).error(), project::ControlValidationError::InvalidBenchmarkConfiguration);
  }

  TEST(ControlProtocolTest, EscapesReplyErrorsAndSerializesMetricsEvent)
  {
    project::ControlReply reply;
    reply.request_id = "request\"id";
    reply.error = "bad\nconfiguration";
    EXPECT_EQ(project::to_json(reply),
              "{\"api_version\":1,\"request_id\":\"request\\\"id\",\"accepted\":false,\"error\":\"bad\\nconfiguration\"}");

    project::SwitchMetricsEvent event;
    event.event_sequence = 9;
    event.topology_revision = 4;
    event.metrics.received_packets = 10;
    event.metrics.forwarded_packets = 8;
    event.metrics.dropped_packets = 2;
    const auto json = project::to_json(event);
    EXPECT_NE(json.find("\"event_sequence\":9"), std::string::npos);
    EXPECT_NE(json.find("\"received_packets\":10"), std::string::npos);
    EXPECT_NE(json.find("\"dropped_packets\":2"), std::string::npos);
  }

  TEST(ControlProtocolTest, ParsesVersionedBenchmarkRequest)
  {
    const std::string json =
      R"({"api_version":1,"request_id":"bench-\u03bb","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":2048,"duration_seconds":60,"seed":42}})";

    const auto parsed = project::control_request_from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value().api_version, 1U);
    EXPECT_EQ(parsed.value().request_id, "bench-\xCE\xBB");
    EXPECT_EQ(parsed.value().command, project::ControlCommand::StartBenchmark);
    EXPECT_EQ(parsed.value().topology_revision, 7U);
    EXPECT_EQ(parsed.value().benchmark.scenario, "mixed-traffic");
    EXPECT_EQ(parsed.value().benchmark.backend, project::AnalyzerBackend::Cpu);
    EXPECT_EQ(parsed.value().benchmark.batch_size, 2048U);
    EXPECT_EQ(parsed.value().benchmark.duration_seconds, 60U);
    EXPECT_EQ(parsed.value().benchmark.seed, 42U);
    EXPECT_EQ(project::to_json(parsed.value()),
              R"({"api_version":1,"request_id":"bench-λ","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":2048,"duration_seconds":60,"seed":42}})");
  }

  TEST(ControlProtocolTest, RejectsMalformedAndIncompleteRequests)
  {
    const auto malformed = project::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"get_switch_state","topology_revision":0)");
    ASSERT_FALSE(malformed.has_value());
    EXPECT_EQ(malformed.error(), project::ControlParseError::MalformedJson);

    const auto missing_parameters = project::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"start_benchmark","topology_revision":0})");
    ASSERT_FALSE(missing_parameters.has_value());
    EXPECT_EQ(missing_parameters.error(), project::ControlParseError::MissingRequiredField);

    const auto invalid_backend = project::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"automatic","batch_size":1,"duration_seconds":1,"seed":1}})");
    ASSERT_FALSE(invalid_backend.has_value());
    EXPECT_EQ(invalid_backend.error(), project::ControlParseError::InvalidField);

    const auto unexpected_parameters = project::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"get_switch_state","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":1,"duration_seconds":1,"seed":1}})");
    ASSERT_FALSE(unexpected_parameters.has_value());
    EXPECT_EQ(unexpected_parameters.error(), project::ControlParseError::InvalidField);
  }
}
