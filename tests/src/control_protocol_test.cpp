#include <chrono>
#include <limits>
#include <string>

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
    request.benchmark.packet_count = 100000;
    request.benchmark.frame_size = 64;

    EXPECT_TRUE(wirelab::validate(request).has_value());
    EXPECT_EQ(wirelab::to_json(request),
              "{\"api_version\":1,\"request_id\":\"benchmark-42\",\"command\":\"start_benchmark\",\"topology_revision\":7,\"parameters\":{\"scenario\":\"mixed-traffic\",\"backend\":\"cpu\",\"generator\":\"cpu\",\"batch_size\":2048,\"duration_seconds\":60,\"seed\":42,\"packets\":100000,\"frame_size\":64}}");
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
    request.benchmark.packet_count = 1000;
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
    // A request that names no generator is a CPU run, so a client written
    // before GPU generation existed still means what it said.
    EXPECT_EQ(parsed.value().benchmark.generator, "cpu");
    EXPECT_EQ(wirelab::to_json(parsed.value()),
              R"({"api_version":1,"request_id":"bench-λ","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"mixed-traffic","backend":"cpu","generator":"cpu","batch_size":2048,"duration_seconds":60,"seed":42,"packets":0,"frame_size":64}})");
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

  TEST(ControlProtocolTest, RoundTripsBenchmarkPacketCountAndFrameSize)
  {
    const std::string json =
      R"({"api_version":1,"request_id":"bench-2","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"broadcast","backend":"cuda","generator":"metal","batch_size":256,"duration_seconds":30,"seed":7,"packets":250000,"frame_size":1500}})";

    const auto parsed = wirelab::control_request_from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->benchmark.packet_count, 250000U);
    EXPECT_EQ(parsed->benchmark.frame_size, 1500U);
    EXPECT_EQ(parsed->benchmark.generator, "metal");
    EXPECT_TRUE(wirelab::validate(parsed.value()).has_value());
    EXPECT_EQ(wirelab::to_json(parsed.value()), json);

    // The two new parameters are read like every other one: a value of the
    // wrong type is a field error, not a missing field.
    const auto invalid_packets = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"bench-3","command":"start_benchmark","topology_revision":7,"parameters":{"scenario":"broadcast","backend":"cpu","batch_size":1,"duration_seconds":1,"seed":1,"packets":"many"}})");
    ASSERT_FALSE(invalid_packets.has_value());
    EXPECT_EQ(invalid_packets.error(), wirelab::ControlParseError::InvalidField);

    // A benchmark parameter on a command that takes none is still refused,
    // which is what keeps a typo from being silently ignored.
    const auto unexpected_frame_size = wirelab::control_request_from_json(
      R"({"api_version":1,"request_id":"bench-4","command":"get_switch_state","topology_revision":0,"parameters":{"frame_size":64}})");
    ASSERT_FALSE(unexpected_frame_size.has_value());
    EXPECT_EQ(unexpected_frame_size.error(), wirelab::ControlParseError::InvalidField);
  }

  TEST(ControlProtocolTest, RejectsBenchmarkConfigurationsThatCannotBeRun)
  {
    wirelab::ControlRequest request;
    request.request_id = "bench-5";
    request.command = wirelab::ControlCommand::StartBenchmark;
    request.benchmark.scenario = "mixed-traffic";
    request.benchmark.duration_seconds = 10;
    request.benchmark.batch_size = 64;
    request.benchmark.frame_size = 64;
    request.benchmark.packet_count = 1000;
    ASSERT_TRUE(wirelab::validate(request).has_value());

    auto without_packets = request;
    without_packets.benchmark.packet_count = 0;
    EXPECT_EQ(wirelab::validate(without_packets).error(), wirelab::ControlValidationError::InvalidBenchmarkConfiguration);

    auto without_batches = request;
    without_batches.benchmark.batch_size = 0;
    EXPECT_EQ(wirelab::validate(without_batches).error(), wirelab::ControlValidationError::InvalidBenchmarkConfiguration);

    auto undersized_frame = request;
    undersized_frame.benchmark.frame_size = 13;
    EXPECT_EQ(wirelab::validate(undersized_frame).error(), wirelab::ControlValidationError::InvalidBenchmarkConfiguration);

    auto oversized_frame = request;
    oversized_frame.benchmark.frame_size = 9001;
    EXPECT_EQ(wirelab::validate(oversized_frame).error(), wirelab::ControlValidationError::InvalidBenchmarkConfiguration);

    // The frame bounds are inclusive: an Ethernet header and a jumbo frame are
    // both runnable.
    auto smallest_frame = request;
    smallest_frame.benchmark.frame_size = 14;
    EXPECT_TRUE(wirelab::validate(smallest_frame).has_value());

    auto largest_frame = request;
    largest_frame.benchmark.frame_size = 9000;
    EXPECT_TRUE(wirelab::validate(largest_frame).has_value());

    // A stale api_version still outranks a benchmark that cannot be run.
    auto stale = without_packets;
    stale.api_version = wirelab::WIRELAB_CONTROL_API_VERSION + 1;
    EXPECT_EQ(wirelab::validate(stale).error(), wirelab::ControlValidationError::UnsupportedApiVersion);
  }

  TEST(ControlProtocolTest, SerializesBenchmarkProgressEvent)
  {
    wirelab::BenchmarkProgressEvent event;
    event.event_sequence = 21;
    event.topology_revision = 4;
    event.operation_id = "benchmark-9";
    event.completed_packets = 40000;
    event.total_packets = 100000;

    EXPECT_EQ(wirelab::to_json(event),
              "{\"api_version\":1,\"event_sequence\":21,\"topology_revision\":4,\"event\":\"benchmark_progress\",\"operation_id\":\"benchmark-9\",\"completed_packets\":40000,\"total_packets\":100000}");
  }

  TEST(ControlProtocolTest, SerializesBenchmarkResultEvent)
  {
    wirelab::BenchmarkResultEvent event;
    event.event_sequence = 22;
    event.topology_revision = 4;
    event.operation_id = "benchmark-9";
    event.completed = true;
    event.result.backend = "cpu";
    event.result.scenario = "mixed-traffic";
    event.result.seed = 42;
    event.result.frame_size = 64;
    event.result.batch_size = 256;
    event.result.total_packets = 100000;
    event.result.completed_packets = 100000;
    event.result.received_packets = 99000;
    event.result.received_bytes = 6336000;
    event.result.malformed_packets = 10;
    event.result.broadcast_packets = 1000;
    event.result.unknown_unicast_packets = 2000;
    event.result.known_unicast_packets = 96000;
    event.result.elapsed_ns = 2000000000;
    event.result.packets_per_second = 50000.5;
    event.result.goodput_bits_per_second = 25344000.25;
    event.result.loss_percentage = 1.125;
    event.result.batch_analysis_latency_p50_ns = 1200;
    event.result.batch_analysis_latency_p95_ns = 2400;
    event.result.batch_analysis_latency_p99_ns = 4800;
    event.result.timing.host_to_device_ns = 100;
    event.result.timing.kernel_ns = 200;
    event.result.timing.device_to_host_ns = 300;

    EXPECT_EQ(wirelab::to_json(event),
              "{\"api_version\":1,\"event_sequence\":22,\"topology_revision\":4,\"event\":\"benchmark_result\",\"operation_id\":\"benchmark-9\",\"completed\":true,\"result\":{\"backend\":\"cpu\",\"scenario\":\"mixed-traffic\",\"seed\":42,\"frame_size\":64,\"batch_size\":256,\"total_packets\":100000,\"completed_packets\":100000,\"received_packets\":99000,\"received_bytes\":6336000,\"malformed_packets\":10,\"broadcast_packets\":1000,\"unknown_unicast_packets\":2000,\"known_unicast_packets\":96000,\"elapsed_ns\":2000000000,\"packets_per_second\":50000.500000,\"goodput_bits_per_second\":25344000.250000,\"loss_percentage\":1.125000,\"batch_analysis_latency_p50_ns\":1200,\"batch_analysis_latency_p95_ns\":2400,\"batch_analysis_latency_p99_ns\":4800,\"timing\":{\"host_to_device_ns\":100,\"kernel_ns\":200,\"device_to_host_ns\":300}}}");

    // A run that was stopped reports what it managed, and says so; a rate that
    // no elapsed time could be divided by is reported as zero rather than as a
    // number no JSON parser accepts.
    wirelab::BenchmarkResultEvent stopped;
    stopped.event_sequence = 23;
    stopped.topology_revision = 4;
    stopped.operation_id = "benchmark-10";
    stopped.result.backend = "cpu";
    stopped.result.scenario = "broadcast";
    stopped.result.total_packets = 100000;
    stopped.result.completed_packets = 512;
    stopped.result.packets_per_second = std::numeric_limits<double>::infinity();
    stopped.result.loss_percentage = std::numeric_limits<double>::quiet_NaN();

    const auto stopped_json = wirelab::to_json(stopped);
    EXPECT_NE(stopped_json.find("\"event\":\"benchmark_result\""), std::string::npos);
    EXPECT_NE(stopped_json.find("\"completed\":false"), std::string::npos);
    EXPECT_NE(stopped_json.find("\"completed_packets\":512"), std::string::npos);
    EXPECT_NE(stopped_json.find("\"packets_per_second\":0.000000"), std::string::npos);
    EXPECT_NE(stopped_json.find("\"loss_percentage\":0.000000"), std::string::npos);
  }
}
