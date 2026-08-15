#include <chrono>

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

  TEST(ControlProtocolTest, ParsesAndSerializesTopologyLoadCommand)
  {
    project::ControlRequest request;
    request.request_id = "topology-42";
    request.command = project::ControlCommand::LoadTopology;
    request.topology.path = "scenarios/security-lab.yaml";

    const auto json = project::to_json(request);
    EXPECT_EQ(json,
              "{\"api_version\":1,\"request_id\":\"topology-42\",\"command\":\"load_topology\",\"topology_revision\":0,\"parameters\":{\"topology_path\":\"scenarios/security-lab.yaml\"}}");

    const auto parsed = project::control_request_from_json(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, project::ControlCommand::LoadTopology);
    EXPECT_EQ(parsed->topology.path, "scenarios/security-lab.yaml");
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

    project::FaultStateEvent fault_event;
    fault_event.event_sequence = 10;
    fault_event.topology_revision = 4;
    fault_event.first_endpoint = "client-a";
    fault_event.configuration.blackhole = true;
    fault_event.active = true;
    const auto fault_json = project::to_json(fault_event);
    EXPECT_NE(fault_json.find("\"event\":\"fault_state_changed\""), std::string::npos);
    EXPECT_NE(fault_json.find("\"first_endpoint\":\"client-a\""), std::string::npos);
    EXPECT_NE(fault_json.find("\"blackhole\":true"), std::string::npos);
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
  TEST(ControlProtocolTest, SerializesTopologyStateEvent)
  {
    project::TopologyStateEvent event;
    event.event_sequence = 12;
    event.topology_revision = 3;
    event.name = "security-lab";
    event.nodes = { { "client-a", project::TopologyNodeType::Host },
                    { "core-switch", project::TopologyNodeType::Switch } };
    event.links = { { "client-a", "core-switch", std::chrono::milliseconds(1) } };

    EXPECT_EQ(project::to_json(event),
              "{\"api_version\":1,\"event_sequence\":12,\"topology_revision\":3,\"event\":\"topology_loaded\",\"name\":\"security-lab\",\"nodes\":[{\"id\":\"client-a\",\"type\":\"host\"},{\"id\":\"core-switch\",\"type\":\"switch\"}],\"links\":[{\"from\":\"client-a\",\"to\":\"core-switch\",\"latency_ms\":1}]}");
  }

  TEST(ControlProtocolTest, ParsesAndSerializesFaultCommands)
  {
    const auto parsed = project::control_request_from_json(
        R"({"parameters":{"isolated":false,"port_id":"client-a","bandwidth_bits_per_second":1000000,"blackhole":false,"loss_basis_points":5,"latency_ms":3,"duplication_basis_points":7,"jitter_ms":1},"topology_revision":2,"command":"set_port_fault","request_id":"fault-1","api_version":1})");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, project::ControlCommand::SetPortFault);
    EXPECT_EQ(parsed->fault.port_id, "client-a");
    EXPECT_EQ(parsed->fault.configuration.latency, std::chrono::milliseconds(3));
    EXPECT_EQ(parsed->fault.configuration.jitter, std::chrono::milliseconds(1));
    EXPECT_EQ(parsed->fault.configuration.loss_basis_points, 5U);
    EXPECT_EQ(parsed->fault.configuration.duplication_basis_points, 7U);
    EXPECT_EQ(parsed->fault.configuration.bandwidth_bits_per_second, 1000000U);
    EXPECT_EQ(
        project::to_json(parsed.value()),
        R"({"api_version":1,"request_id":"fault-1","command":"set_port_fault","topology_revision":2,"parameters":{"port_id":"client-a","latency_ms":3,"jitter_ms":1,"loss_basis_points":5,"duplication_basis_points":7,"bandwidth_bits_per_second":1000000,"blackhole":false,"isolated":false}})");

    const auto incomplete = project::control_request_from_json(
        R"({"api_version":1,"request_id":"fault-2","command":"set_port_fault","topology_revision":2,"parameters":{"port_id":"client-a"}})");
    ASSERT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error(), project::ControlParseError::MissingRequiredField);
  }
}
