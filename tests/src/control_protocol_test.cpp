#include <chrono>

#include <gtest/gtest.h>

#include "wirelab/control_protocol.hpp"

namespace
{
  TEST(ControlProtocolTest, ValidatesVersionedBenchmarkRequest)
  {
    wirelab::ControlRequest request;
    request.request_id = "benchmark-42";
    request.command = wirelab::ControlCommand::StartBenchmark;
    request.topology_revision = 7;
    request.benchmark.scenario = "mixed-traffic";
    request.benchmark.backend = wirelab::AnalyzerBackend::Cpu;
    request.benchmark.batch_size = 2048;
    request.benchmark.duration_seconds = 60;
    request.benchmark.seed = 42;

    EXPECT_TRUE(wirelab::validate(request).has_value());
    EXPECT_EQ(wirelab::to_json(request),
              "{\"api_version\":1,\"request_id\":\"benchmark-42\",\"command\":\"start_benchmark\",\"topology_revision\":7,\"parameters\":{\"scenario\":\"mixed-traffic\",\"backend\":\"cpu\",\"batch_size\":2048,\"duration_seconds\":60,\"seed\":42}}");
  }

  TEST(ControlProtocolTest, ParsesAndSerializesTopologyLoadCommand)
  {
    wirelab::ControlRequest request;
    request.request_id = "topology-42";
    request.command = wirelab::ControlCommand::LoadTopology;
    request.topology.path = "scenarios/security-lab.yaml";

    const auto json = wirelab::to_json(request);
    EXPECT_EQ(json,
              "{\"api_version\":1,\"request_id\":\"topology-42\",\"command\":\"load_topology\",\"topology_revision\":0,\"parameters\":{\"topology_path\":\"scenarios/security-lab.yaml\"}}");

    const auto parsed = wirelab::control_request_from_json(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, wirelab::ControlCommand::LoadTopology);
    EXPECT_EQ(parsed->topology.path, "scenarios/security-lab.yaml");
  }

  TEST(ControlProtocolTest, ParsesAndSerializesActiveFaultsCommand)
  {
    wirelab::ControlRequest request;
    request.request_id = "faults-42";
    request.command = wirelab::ControlCommand::GetActiveFaults;
    request.topology_revision = 3;

    const auto json = wirelab::to_json(request);
    EXPECT_EQ(json,
              "{\"api_version\":1,\"request_id\":\"faults-42\",\"command\":\"get_active_faults\",\"topology_revision\":3}");

    const auto parsed = wirelab::control_request_from_json(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, wirelab::ControlCommand::GetActiveFaults);
  }

  TEST(ControlProtocolTest, ParsesAndSerializesSupervisionStateCommand)
  {
    wirelab::ControlRequest request;
    request.request_id = "supervision-42";
    request.command = wirelab::ControlCommand::GetSupervisionState;
    request.topology_revision = 5;

    const auto json = wirelab::to_json(request);
    EXPECT_EQ(json,
              "{\"api_version\":1,\"request_id\":\"supervision-42\",\"command\":\"get_supervision_state\",\"topology_revision\":5}");

    const auto parsed = wirelab::control_request_from_json(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, wirelab::ControlCommand::GetSupervisionState);

    // The query takes no parameters, and a client that sends some is told so
    // rather than having them ignored.
    const auto parameterised = wirelab::control_request_from_json(
        R"({"api_version":1,"request_id":"supervision-43","command":"get_supervision_state","topology_revision":5,"parameters":{"port_id":"client-a"}})");
    ASSERT_FALSE(parameterised.has_value());
    EXPECT_EQ(parameterised.error(), wirelab::ControlParseError::InvalidField);
  }

  TEST(ControlProtocolTest, RejectsUnsupportedVersionAndInvalidBenchmark)
  {
    wirelab::ControlRequest request;
    request.request_id = "request";
    request.api_version = wirelab::WIRELAB_CONTROL_API_VERSION + 1;
    EXPECT_EQ(wirelab::validate(request).error(), wirelab::ControlValidationError::UnsupportedApiVersion);

    request.api_version = wirelab::WIRELAB_CONTROL_API_VERSION;
    request.command = wirelab::ControlCommand::StartBenchmark;
    request.benchmark.scenario = "mixed-traffic";
    request.benchmark.duration_seconds = 1;
    request.benchmark.batch_size = 0;
    EXPECT_EQ(wirelab::validate(request).error(), wirelab::ControlValidationError::InvalidBenchmarkConfiguration);
  }

  TEST(ControlProtocolTest, EscapesReplyErrorsAndSerializesMetricsEvent)
  {
    wirelab::ControlReply reply;
    reply.request_id = "request\"id";
    reply.error = "bad\nconfiguration";
    reply.topology_revision = 6;
    EXPECT_EQ(wirelab::to_json(reply),
              "{\"api_version\":1,\"request_id\":\"request\\\"id\",\"accepted\":false,\"topology_revision\":6,\"error\":\"bad\\nconfiguration\"}");

    wirelab::SwitchMetricsEvent event;
    event.event_sequence = 9;
    event.topology_revision = 4;
    event.metrics.received_packets = 10;
    event.metrics.forwarded_packets = 8;
    event.metrics.dropped_packets = 2;
    const auto json = wirelab::to_json(event);
    EXPECT_NE(json.find("\"event_sequence\":9"), std::string::npos);
    EXPECT_NE(json.find("\"received_packets\":10"), std::string::npos);
    EXPECT_NE(json.find("\"dropped_packets\":2"), std::string::npos);

    wirelab::FaultStateEvent fault_event;
    fault_event.event_sequence = 10;
    fault_event.topology_revision = 4;
    fault_event.first_endpoint = "client-a";
    fault_event.configuration.blackhole = true;
    fault_event.active = true;
    const auto fault_json = wirelab::to_json(fault_event);
    EXPECT_NE(fault_json.find("\"event\":\"fault_state_changed\""), std::string::npos);
    EXPECT_NE(fault_json.find("\"first_endpoint\":\"client-a\""), std::string::npos);
    EXPECT_NE(fault_json.find("\"blackhole\":true"), std::string::npos);
  }

  TEST(ControlProtocolTest, ParsesVersionedBenchmarkRequest)
  {
    const std::string json =
      R"({"api_version":1,"request_id":"bench-\u03bb","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":2048,"duration_seconds":60,"seed":42}})";

    const auto parsed = wirelab::control_request_from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value().api_version, 1U);
    EXPECT_EQ(parsed.value().request_id, "bench-\xCE\xBB");
    EXPECT_EQ(parsed.value().command, wirelab::ControlCommand::StartBenchmark);
    EXPECT_EQ(parsed.value().topology_revision, 7U);
    EXPECT_EQ(parsed.value().benchmark.scenario, "mixed-traffic");
    EXPECT_EQ(parsed.value().benchmark.backend, wirelab::AnalyzerBackend::Cpu);
    EXPECT_EQ(parsed.value().benchmark.batch_size, 2048U);
    EXPECT_EQ(parsed.value().benchmark.duration_seconds, 60U);
    EXPECT_EQ(parsed.value().benchmark.seed, 42U);
    EXPECT_EQ(wirelab::to_json(parsed.value()),
              R"({"api_version":1,"request_id":"bench-λ","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":2048,"duration_seconds":60,"seed":42}})");
  }

  TEST(ControlProtocolTest, RejectsMalformedAndIncompleteRequests)
  {
    const auto malformed = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"get_switch_state","topology_revision":0)");
    ASSERT_FALSE(malformed.has_value());
    EXPECT_EQ(malformed.error(), wirelab::ControlParseError::MalformedJson);

    const auto missing_parameters = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"start_benchmark","topology_revision":0})");
    ASSERT_FALSE(missing_parameters.has_value());
    EXPECT_EQ(missing_parameters.error(), wirelab::ControlParseError::MissingRequiredField);

    const auto invalid_backend = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"automatic","batch_size":1,"duration_seconds":1,"seed":1}})");
    ASSERT_FALSE(invalid_backend.has_value());
    EXPECT_EQ(invalid_backend.error(), wirelab::ControlParseError::InvalidField);

    const auto unexpected_parameters = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"request","command":"get_switch_state","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":1,"duration_seconds":1,"seed":1}})");
    ASSERT_FALSE(unexpected_parameters.has_value());
    EXPECT_EQ(unexpected_parameters.error(), wirelab::ControlParseError::InvalidField);
  }
  TEST(ControlProtocolTest, SerializesTopologyStateEvent)
  {
    wirelab::TopologyStateEvent event;
    event.event_sequence = 12;
    event.topology_revision = 3;
    event.name = "security-lab";
    event.nodes = { { "client-a", wirelab::TopologyNodeType::Host },
                    { "core-switch", wirelab::TopologyNodeType::Switch } };
    event.links = { { "client-a", "core-switch", std::chrono::milliseconds(1) } };

    EXPECT_EQ(wirelab::to_json(event),
              "{\"api_version\":1,\"event_sequence\":12,\"topology_revision\":3,\"event\":\"topology_loaded\",\"name\":\"security-lab\",\"nodes\":[{\"id\":\"client-a\",\"type\":\"host\"},{\"id\":\"core-switch\",\"type\":\"switch\"}],\"links\":[{\"from\":\"client-a\",\"to\":\"core-switch\",\"latency_ms\":1}]}");
  }

  TEST(ControlProtocolTest, SerializesAnomalyAndPolicyActionEvents)
  {
    wirelab::AnomalyDetectedEvent anomaly_event;
    anomaly_event.event_sequence = 13;
    anomaly_event.topology_revision = 3;
    anomaly_event.anomaly.type = wirelab::AnomalyType::UdpFlood;
    anomaly_event.anomaly.source_mac = wirelab::MacAddress::from_string("00:11:22:33:44:55");
    anomaly_event.anomaly.source_ipv4 = 0xC0A80101;
    anomaly_event.anomaly.ingress_port = 7;
    anomaly_event.anomaly.observed_packets = 51'000;
    anomaly_event.anomaly.observed_bytes = 4'080'000;
    anomaly_event.anomaly.threshold = 50'000;
    anomaly_event.anomaly.window_duration_ns = 1'000'000'000;

    EXPECT_EQ(wirelab::to_json(anomaly_event),
              "{\"api_version\":1,\"event_sequence\":13,\"topology_revision\":3,\"event\":\"anomaly_detected\",\"anomaly\":{\"type\":\"udp_flood\",\"source_mac\":\"00:11:22:33:44:55\",\"source_ipv4\":3232235777,\"ingress_port\":7,\"observed_packets\":51000,\"observed_bytes\":4080000,\"observed_distinct_destinations\":0,\"threshold\":50000,\"window_duration_ns\":1000000000}}");

    wirelab::PolicyActionEvent policy_event;
    policy_event.event_sequence = 14;
    policy_event.topology_revision = 3;
    policy_event.decision.rule_name = "contain-udp-flood";
    policy_event.decision.action = wirelab::PolicyAction::RateLimit;
    policy_event.decision.hit_count = 2;
    policy_event.decision.rate_limit_packets_per_second = 50'000;
    policy_event.decision.anomaly = anomaly_event.anomaly;

    EXPECT_EQ(wirelab::to_json(policy_event),
              "{\"api_version\":1,\"event_sequence\":14,\"topology_revision\":3,\"event\":\"policy_action\",\"rule_name\":\"contain-udp-flood\",\"action\":\"rate_limit\",\"hit_count\":2,\"rate_limit_packets_per_second\":50000,\"anomaly\":{\"type\":\"udp_flood\",\"source_mac\":\"00:11:22:33:44:55\",\"source_ipv4\":3232235777,\"ingress_port\":7,\"observed_packets\":51000,\"observed_bytes\":4080000,\"observed_distinct_destinations\":0,\"threshold\":50000,\"window_duration_ns\":1000000000}}");
  }

  TEST(ControlProtocolTest, ParsesAndSerializesFaultCommands)
  {
    const auto parsed = wirelab::control_request_from_json(
        R"({"parameters":{"isolated":false,"port_id":"client-a","bandwidth_bits_per_second":1000000,"blackhole":false,"loss_basis_points":5,"latency_ms":3,"duplication_basis_points":7,"jitter_ms":1},"topology_revision":2,"command":"set_port_fault","request_id":"fault-1","api_version":1})");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->command, wirelab::ControlCommand::SetPortFault);
    EXPECT_EQ(parsed->fault.port_id, "client-a");
    EXPECT_EQ(parsed->fault.configuration.latency, std::chrono::milliseconds(3));
    EXPECT_EQ(parsed->fault.configuration.jitter, std::chrono::milliseconds(1));
    EXPECT_EQ(parsed->fault.configuration.loss_basis_points, 5U);
    EXPECT_EQ(parsed->fault.configuration.duplication_basis_points, 7U);
    EXPECT_EQ(parsed->fault.configuration.bandwidth_bits_per_second, 1000000U);
    EXPECT_EQ(
        wirelab::to_json(parsed.value()),
        R"({"api_version":1,"request_id":"fault-1","command":"set_port_fault","topology_revision":2,"parameters":{"port_id":"client-a","latency_ms":3,"jitter_ms":1,"loss_basis_points":5,"duplication_basis_points":7,"bandwidth_bits_per_second":1000000,"blackhole":false,"isolated":false}})");

    const auto incomplete = wirelab::control_request_from_json(
        R"({"api_version":1,"request_id":"fault-2","command":"set_port_fault","topology_revision":2,"parameters":{"port_id":"client-a"}})");
    ASSERT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error(), wirelab::ControlParseError::MissingRequiredField);
  }

  TEST(ControlProtocolTest, ParsesRepliesAndRefusesEvents)
  {
    wirelab::ControlReply reply;
    reply.request_id = "state-1";
    reply.accepted = true;
    reply.topology_revision = 4;
    reply.operation_id = "switch-state-9";

    const auto parsed = wirelab::control_reply_from_json(wirelab::to_json(reply));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->request_id, "state-1");
    EXPECT_TRUE(parsed->accepted);
    EXPECT_EQ(parsed->topology_revision, 4U);
    EXPECT_EQ(parsed->operation_id, "switch-state-9");

    // A rejection still carries the revision, which is the whole point: it is
    // how a reconnected client learns what the switch moved on to.
    wirelab::ControlReply rejection;
    rejection.request_id = "state-2";
    rejection.topology_revision = 11;
    rejection.error = "stale topology revision";

    const auto parsed_rejection = wirelab::control_reply_from_json(wirelab::to_json(rejection));
    ASSERT_TRUE(parsed_rejection.has_value());
    EXPECT_FALSE(parsed_rejection->accepted);
    EXPECT_EQ(parsed_rejection->topology_revision, 11U);
    EXPECT_EQ(parsed_rejection->error, "stale topology revision");

    // Events share the channel with replies, and telling them apart is what
    // lets a client resynchronise without losing what the switch reported
    // while it did.
    wirelab::SupervisionStateEvent event;
    event.event_sequence = 3;
    event.topology_revision = 4;
    EXPECT_FALSE(wirelab::control_reply_from_json(wirelab::to_json(event)).has_value());
  }
}
