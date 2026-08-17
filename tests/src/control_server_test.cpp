#include "wirelab/control_server.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "wirelab/switch_supervisor.hpp"

namespace
{
  using namespace std::chrono_literals;

  wirelab::VSwitch make_switch()
  {
    auto created = wirelab::VSwitch::create(0, wirelab::VSwitchLogLevel::Lifecycle);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "control-lab";
    configuration.nodes = { { "client-a", wirelab::TopologyNodeType::Host },
                            { "client-b", wirelab::TopologyNodeType::Host },
                            { "core-switch", wirelab::TopologyNodeType::Switch } };
    configuration.links = { { "client-a", "core-switch", std::chrono::milliseconds(0) },
                            { "client-b", "core-switch", std::chrono::milliseconds(0) } };
    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
  }

  wirelab::ControlServer make_server(wirelab::ControlService& service)
  {
    auto created = wirelab::ControlServer::create(service, 0);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  wirelab::ControlServer make_server(wirelab::ControlService& service, wirelab::ControlServerConfig config)
  {
    auto created = wirelab::ControlServer::create(service, 0, "127.0.0.1", config);
    EXPECT_TRUE(created.has_value());
    return std::move(created.value());
  }

  wirelab::ControlClient make_client(wirelab::ControlServer& server)
  {
    auto connected = wirelab::ControlClient::connect("127.0.0.1", server.port());
    EXPECT_TRUE(connected.has_value());
    return std::move(connected.value());
  }

  // The server is polled by the switch loop in production; a test drives that
  // loop itself until the connection has been accepted.
  void accept_clients(wirelab::ControlServer& server, size_t expected)
  {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (server.client_count() < expected && std::chrono::steady_clock::now() < deadline)
    {
      (void)server.poll(10ms);
    }
    EXPECT_EQ(server.client_count(), expected);
  }

  std::vector<std::string> collect(wirelab::ControlServer& server, wirelab::ControlClient& client, size_t expected)
  {
    std::vector<std::string> messages;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (messages.size() < expected && std::chrono::steady_clock::now() < deadline)
    {
      (void)server.poll(5ms);
      auto received = client.receive(5ms);
      if (!received)
      {
        break;
      }
      if (received.value())
      {
        messages.push_back(*received.value());
      }
    }
    return messages;
  }

  bool contains(const std::string& message, const std::string& needle)
  {
    return message.find(needle) != std::string::npos;
  }

  std::vector<uint8_t> broadcast_frame(uint8_t source_suffix)
  {
    const wirelab::MacAddress source({ 0x02, 0x00, 0x00, 0x00, 0x00, source_suffix });
    wirelab::EthernetFrame frame(wirelab::MacAddress::broadcast(), source, 0x0800, std::vector<uint8_t>(46, 0x5a));
    return frame.serialize();
  }

  wirelab::AnomalyDetectorConfig storm_config()
  {
    wirelab::AnomalyDetectorConfig config;
    config.window_duration_ns = 1'000'000'000;
    config.broadcast_packets_threshold = 4;
    return config;
  }

  TEST(ControlServerTest, AnswersARequestAndBroadcastsTheEventItProduced)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 3);
    auto server = make_server(service);
    ASSERT_NE(server.port(), 0);
    auto client = make_client(server);

    ASSERT_TRUE(
        client.send(R"({"api_version":1,"request_id":"state-1","command":"get_switch_state","topology_revision":3})"));

    const auto messages = collect(server, client, 2);
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_TRUE(contains(messages[0], R"("request_id":"state-1")"));
    EXPECT_TRUE(contains(messages[0], R"("accepted":true)"));
    EXPECT_TRUE(contains(messages[1], R"("event":"switch_metrics")"));
    EXPECT_TRUE(contains(messages[1], R"("topology_revision":3)"));
  }

  TEST(ControlServerTest, RunsABenchmarkAcrossPollsAndFansTheResultOut)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    wirelab::ControlServerConfig config;
    // A budget well below the run forces the server to slice it over several
    // polls, which is the property the switch loop depends on.
    config.benchmark_packets_per_poll = 16;
    auto server = make_server(service, config);
    auto client = make_client(server);

    ASSERT_TRUE(client.send(
        R"({"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":0,"parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":8,"duration_seconds":60,"seed":42,"packets":64,"frame_size":64}})"));

    // reply, zero progress, four progress slices, result.
    const auto messages = collect(server, client, 7);
    ASSERT_GE(messages.size(), 3U);
    EXPECT_TRUE(contains(messages[0], R"("request_id":"bench-1")"));
    EXPECT_TRUE(contains(messages[0], R"("accepted":true)"));
    EXPECT_TRUE(contains(messages[0], R"("operation_id":"benchmark-1")"));
    EXPECT_TRUE(contains(messages[1], R"("event":"benchmark_progress")"));
    EXPECT_TRUE(contains(messages[1], R"("completed_packets":0)"));

    const auto& last = messages.back();
    EXPECT_TRUE(contains(last, R"("event":"benchmark_result")"));
    EXPECT_TRUE(contains(last, R"("completed":true)"));
    EXPECT_TRUE(contains(last, R"("operation_id":"benchmark-1")"));
    EXPECT_TRUE(contains(last, R"("completed_packets":64)"));
    EXPECT_FALSE(service.benchmark_active());
  }

  TEST(ControlServerTest, RejectsAMalformedRequestWithoutDroppingTheConnection)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    auto server = make_server(service);
    auto client = make_client(server);

    ASSERT_TRUE(client.send("not json"));
    const auto rejection = collect(server, client, 1);
    ASSERT_EQ(rejection.size(), 1U);
    EXPECT_TRUE(contains(rejection[0], R"("accepted":false)"));
    EXPECT_TRUE(contains(rejection[0], "malformed JSON"));

    // The connection survives a bad line, so an operator's typo does not cost
    // them their event stream.
    EXPECT_EQ(server.client_count(), 1U);
    ASSERT_TRUE(
        client.send(R"({"api_version":1,"request_id":"state-2","command":"get_switch_state","topology_revision":0})"));
    const auto accepted = collect(server, client, 2);
    ASSERT_EQ(accepted.size(), 2U);
    EXPECT_TRUE(contains(accepted[0], R"("request_id":"state-2")"));
    EXPECT_TRUE(contains(accepted[0], R"("accepted":true)"));
  }

  TEST(ControlServerTest, SplitsTwoRequestsDeliveredInOneWrite)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    auto server = make_server(service);
    auto client = make_client(server);

    // One write, two messages: the framing is the newline, not the packet.
    ASSERT_TRUE(client.send(
        std::string(R"({"api_version":1,"request_id":"one","command":"get_switch_state","topology_revision":0})") + "\n" +
        R"({"api_version":1,"request_id":"two","command":"get_switch_state","topology_revision":0})"));

    const auto messages = collect(server, client, 4);
    ASSERT_EQ(messages.size(), 4U);
    EXPECT_TRUE(contains(messages[0], R"("request_id":"one")"));
    EXPECT_TRUE(contains(messages[2], R"("request_id":"two")"));
  }

  TEST(ControlServerTest, AppliesAFaultCommandToTheTopologyAndTellsEveryClient)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    auto server = make_server(service);
    auto operator_client = make_client(server);
    auto observer = make_client(server);
    accept_clients(server, 2);

    ASSERT_TRUE(operator_client.send(
        R"({"api_version":1,"request_id":"fault-1","command":"set_port_fault","topology_revision":1,"parameters":{"port_id":"client-a","latency_ms":0,"jitter_ms":0,"loss_basis_points":0,"duplication_basis_points":0,"bandwidth_bits_per_second":0,"blackhole":true,"isolated":false}})"));

    const auto issued = collect(server, operator_client, 2);
    ASSERT_EQ(issued.size(), 2U);
    EXPECT_TRUE(contains(issued[0], R"("accepted":true)"));
    EXPECT_TRUE(contains(issued[1], R"("event":"fault_state_changed")"));

    const auto fault = controller.port_fault("client-a");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->blackhole);

    // A fault is shared state, so the client that did not ask is told as well.
    const auto witnessed = collect(server, observer, 1);
    ASSERT_EQ(witnessed.size(), 1U);
    EXPECT_TRUE(contains(witnessed[0], R"("event":"fault_state_changed")"));
    EXPECT_TRUE(contains(witnessed[0], R"("first_endpoint":"client-a")"));
  }

  TEST(ControlServerTest, PublishesSupervisedEnforcementOntoTheControlChannel)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline(storm_config(), controller);
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "quarantine-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));

    wirelab::SwitchSupervisor supervisor(pipeline, controller);
    auto server = make_server(service);
    supervisor.attach_control(server);
    auto client = make_client(server);
    accept_clients(server, 1);

    const wirelab::Endpoint sender("127.0.0.1", 40001);
    const auto frame = broadcast_frame(0x01);
    const auto now = std::chrono::steady_clock::now();
    for (int index = 0; index < 6; ++index)
    {
      (void)supervisor.inspect(frame, sender, now);
    }
    supervisor.tick(now + 300ms);

    // Anomaly, policy decision, the fault the decision applied, and the
    // supervision counters that moved with them.
    const auto messages = collect(server, client, 4);
    ASSERT_EQ(messages.size(), 4U);
    EXPECT_TRUE(contains(messages[0], R"("event":"anomaly_detected")"));
    EXPECT_TRUE(contains(messages[0], R"("type":"broadcast_storm")"));
    EXPECT_TRUE(contains(messages[1], R"("event":"policy_action")"));
    EXPECT_TRUE(contains(messages[1], R"("action":"quarantine")"));
    EXPECT_TRUE(contains(messages[2], R"("event":"fault_state_changed")"));
    EXPECT_TRUE(contains(messages[2], R"("first_endpoint":"client-a")"));
    EXPECT_TRUE(contains(messages[3], R"("event":"supervision_state")"));
    EXPECT_TRUE(contains(messages[3], R"("analysed_frames":6)"));
    EXPECT_TRUE(contains(messages[3], R"({"port_id":"client-a","endpoint":"127.0.0.1:40001"})"));

    const auto fault = controller.port_fault("client-a");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->isolated);
  }

  TEST(ControlServerTest, RepublishesSupervisionOnlyWhenItMoved)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline(storm_config(), controller);
    wirelab::SwitchSupervisor supervisor(pipeline, controller);
    auto server = make_server(service);
    supervisor.attach_control(server);
    auto client = make_client(server);
    accept_clients(server, 1);

    const wirelab::Endpoint sender("127.0.0.1", 40002);
    const auto now = std::chrono::steady_clock::now();
    (void)supervisor.inspect(broadcast_frame(0x02), sender, now);
    supervisor.tick(now + 300ms);
    const auto first = collect(server, client, 1);
    ASSERT_EQ(first.size(), 1U);
    EXPECT_TRUE(contains(first[0], R"("event":"supervision_state")"));

    // Idle ticks move nothing, so an idle switch says nothing.
    supervisor.tick(now + 600ms);
    supervisor.tick(now + 900ms);
    EXPECT_TRUE(collect(server, client, 1).empty());
  }

  TEST(ControlServerTest, ForgetsAClientThatDisconnects)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    auto server = make_server(service);
    {
      auto client = make_client(server);
      accept_clients(server, 1);
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (server.client_count() != 0 && std::chrono::steady_clock::now() < deadline)
    {
      (void)server.poll(10ms);
    }
    EXPECT_EQ(server.client_count(), 0U);
  }

  TEST(ControlServerTest, AnswersALateJoinerAskingForSupervisionState)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline(storm_config(), controller);
    wirelab::SwitchSupervisor supervisor(pipeline, controller);
    service.set_supervision_source([&supervisor] { return supervisor.supervision_snapshot(); });

    auto server = make_server(service);
    supervisor.attach_control(server);

    // The traffic happens before anyone is listening, so the state that moved
    // was published to nobody.
    const wirelab::Endpoint sender("127.0.0.1", 40004);
    const auto now = std::chrono::steady_clock::now();
    (void)supervisor.inspect(broadcast_frame(0x03), sender, now);
    (void)supervisor.inspect(broadcast_frame(0x03), sender, now);
    supervisor.tick(now + 300ms);

    auto client = make_client(server);
    accept_clients(server, 1);
    ASSERT_TRUE(client.send(
        R"({"api_version":1,"request_id":"supervision-1","command":"get_supervision_state","topology_revision":1})"));

    const auto messages = collect(server, client, 2);
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_TRUE(contains(messages[0], R"("request_id":"supervision-1")"));
    EXPECT_TRUE(contains(messages[0], R"("accepted":true)"));
    EXPECT_TRUE(contains(messages[0], R"("topology_revision":1)"));
    EXPECT_TRUE(contains(messages[1], R"("event":"supervision_state")"));
    EXPECT_TRUE(contains(messages[1], R"("analysed_frames":2)"));
    EXPECT_TRUE(contains(messages[1], R"({"port_id":"client-a","endpoint":"127.0.0.1:40004"})"));
  }

  TEST(ControlServerTest, RefusesSupervisionStateWithoutASupervisor)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    auto server = make_server(service);
    auto client = make_client(server);
    accept_clients(server, 1);

    ASSERT_TRUE(client.send(
        R"({"api_version":1,"request_id":"supervision-2","command":"get_supervision_state","topology_revision":0})"));

    const auto messages = collect(server, client, 1);
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_TRUE(contains(messages[0], R"("accepted":false)"));
    EXPECT_TRUE(contains(messages[0], "supervision state is unavailable"));
  }

  TEST(ControlServerTest, DropsAClientThatStoppedReadingAndKeepsServingTheRest)
  {
    auto vswitch = make_switch();
    wirelab::ControlService service(vswitch, 0);
    wirelab::ControlServerConfig config;
    config.max_pending_bytes = 32 * 1024;
    auto server = make_server(service, config);

    auto reader = make_client(server);
    auto silent = make_client(server);
    accept_clients(server, 2);

    wirelab::SupervisionSnapshot snapshot;
    snapshot.bindings = { { "client-a", "127.0.0.1:40005" } };

    // The silent client never reads. Its kernel buffers fill, then its outbox
    // passes the cap, and the server gives up on it. The reader keeps draining,
    // so it must survive the same broadcasts.
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    uint64_t published = 0;
    while (server.client_count() == 2 && std::chrono::steady_clock::now() < deadline)
    {
      snapshot.analysed_frames = ++published;
      server.publish_supervision(snapshot);
      (void)server.poll(0ms);
      while (true)
      {
        auto received = reader.receive(0ms);
        if (!received || !received.value())
        {
          break;
        }
      }
    }

    ASSERT_EQ(server.client_count(), 1U) << "the switch kept a client that stopped reading, after " << published
                                         << " broadcasts";
    EXPECT_TRUE(silent.is_connected()) << "the client was dropped by the switch, not by itself";

    ASSERT_TRUE(
        reader.send(R"({"api_version":1,"request_id":"state-3","command":"get_switch_state","topology_revision":0})"));
    const auto messages = collect(server, reader, 2);
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_TRUE(contains(messages[0], R"("request_id":"state-3")"));
    EXPECT_TRUE(contains(messages[0], R"("accepted":true)"));
  }

  TEST(ControlServerTest, ResynchronisesAfterReconnecting)
  {
    auto vswitch = make_switch();
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::ControlService service(vswitch, controller);
    wirelab::AnalysisPipeline pipeline(storm_config(), controller);
    wirelab::SwitchSupervisor supervisor(pipeline, controller);
    service.set_supervision_source([&supervisor] { return supervisor.supervision_snapshot(); });

    auto server = make_server(service);
    supervisor.attach_control(server);
    auto client = make_client(server);
    accept_clients(server, 1);

    const wirelab::Endpoint sender("127.0.0.1", 40006);
    const auto now = std::chrono::steady_clock::now();
    (void)supervisor.inspect(broadcast_frame(0x04), sender, now);
    supervisor.tick(now + 300ms);

    // The connection is lost. The client comes back knowing nothing: not the
    // topology revision its commands must carry, not what it missed.
    client.close();
    ASSERT_TRUE(client.reconnect().has_value());
    EXPECT_EQ(client.topology_revision(), 0U);

    std::optional<wirelab::ControlResync> resynchronised;
    std::atomic<bool> finished{ false };
    std::thread worker(
        [&client, &resynchronised, &finished]
        {
          auto outcome = client.resync(5s);
          if (outcome)
          {
            resynchronised = std::move(outcome.value());
          }
          finished = true;
        });

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!finished && std::chrono::steady_clock::now() < deadline)
    {
      (void)server.poll(5ms);
    }
    worker.join();

    ASSERT_TRUE(resynchronised.has_value()) << "the client could not describe the switch it reconnected to";
    // The first query was stale, the rejection carried the revision, and the
    // repeat was answered against it.
    EXPECT_EQ(resynchronised->topology_revision, controller.topology_revision());
    EXPECT_EQ(client.topology_revision(), controller.topology_revision());

    bool saw_metrics = false;
    bool saw_supervision = false;
    for (const auto& message : resynchronised->messages)
    {
      saw_metrics = saw_metrics || contains(message, R"("event":"switch_metrics")");
      saw_supervision = saw_supervision || contains(message, R"("event":"supervision_state")");
    }
    EXPECT_TRUE(saw_metrics);
    EXPECT_TRUE(saw_supervision);
  }
}  // namespace
