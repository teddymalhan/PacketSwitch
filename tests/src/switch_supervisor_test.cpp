#include "wirelab/switch_supervisor.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace
{
  using namespace std::chrono_literals;

  wirelab::Topology make_topology()
  {
    wirelab::TopologyConfiguration configuration;
    configuration.name = "switch-lab";
    configuration.nodes = { { "client-a", wirelab::TopologyNodeType::Host },
                            { "client-b", wirelab::TopologyNodeType::Host },
                            { "core-switch", wirelab::TopologyNodeType::Switch } };
    configuration.links = { { "client-a", "core-switch", std::chrono::milliseconds(0) },
                            { "client-b", "core-switch", std::chrono::milliseconds(0) } };
    auto topology = wirelab::Topology::create(std::move(configuration));
    EXPECT_TRUE(topology.has_value());
    return std::move(topology.value());
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

  // Drains whatever the switch has forwarded to this socket within the budget.
  size_t drain(wirelab::UdpSocket& socket, std::chrono::milliseconds budget)
  {
    size_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline)
    {
      auto readable = socket.wait_readable(10ms);
      if (!readable || !*readable)
      {
        continue;
      }
      if (socket.receive_from())
      {
        ++received;
      }
    }
    return received;
  }

  wirelab::UdpSocket make_client()
  {
    auto created = wirelab::UdpSocket::create();
    EXPECT_TRUE(created.has_value());
    auto socket = std::move(created.value());
    EXPECT_TRUE(socket.bind("127.0.0.1", 0));
    return socket;
  }

  TEST(SwitchSupervisorTest, AttributesSendersToTopologyPortsInFirstSeenOrder)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::AnalysisPipeline pipeline(storm_config());
    wirelab::SwitchSupervisor supervisor(pipeline, controller);

    const auto frame = broadcast_frame(0x01);
    const auto now = std::chrono::steady_clock::now();
    (void)supervisor.inspect(frame, wirelab::Endpoint("127.0.0.1", 4001), now);
    (void)supervisor.inspect(frame, wirelab::Endpoint("127.0.0.1", 4002), now);
    (void)supervisor.inspect(frame, wirelab::Endpoint("127.0.0.1", 4001), now);

    const auto bound = supervisor.bindings();
    ASSERT_EQ(bound.size(), 2U);
    EXPECT_EQ(bound[0].first, "client-a");
    EXPECT_EQ(bound[0].second, wirelab::Endpoint("127.0.0.1", 4001));
    EXPECT_EQ(bound[1].first, "client-b");
    EXPECT_EQ(bound[1].second, wirelab::Endpoint("127.0.0.1", 4002));
  }

  TEST(SwitchSupervisorTest, BlocksFramesFromAPortAPolicyQuarantined)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::AnalysisPipeline pipeline(storm_config());
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));
    wirelab::SwitchSupervisorConfig config;
    config.tick_interval = 10ms;
    wirelab::SwitchSupervisor supervisor(pipeline, controller, config);

    const auto frame = broadcast_frame(0x01);
    const wirelab::Endpoint sender("127.0.0.1", 4001);
    auto now = std::chrono::steady_clock::now();
    for (int index = 0; index < 6; ++index)
    {
      const auto decision = supervisor.inspect(frame, sender, now);
      EXPECT_FALSE(decision.dropped);
    }

    // The batch has not been analysed yet, so nothing is contained until a tick.
    supervisor.tick(now + 1s);

    const auto after = supervisor.inspect(frame, sender, now + 1s);
    EXPECT_TRUE(after.dropped || after.delivery_count == 0);
    EXPECT_EQ(supervisor.blocked_frames(), 1U);
    const auto fault = controller.port_fault("client-a");
    ASSERT_TRUE(fault.has_value());
    EXPECT_TRUE(fault->isolated);
  }

  TEST(SwitchSupervisorTest, QuarantineStopsRealForwardingAndLapsesOnItsOwn)
  {
    wirelab::TopologyController controller;
    controller.load(make_topology());
    wirelab::PolicyEnforcerConfig enforcer_config;
    enforcer_config.quarantine_lease = 400ms;
    wirelab::AnalysisPipeline pipeline(storm_config(), enforcer_config);
    ASSERT_TRUE(pipeline.policies().add_rule(
        { "contain-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine }));
    wirelab::SwitchSupervisorConfig supervisor_config;
    supervisor_config.tick_interval = 20ms;
    supervisor_config.max_batch_frames = 4;
    wirelab::SwitchSupervisor supervisor(pipeline, controller, supervisor_config);

    auto created = wirelab::VSwitch::create(0);
    ASSERT_TRUE(created.has_value());
    wirelab::VSwitch vswitch = std::move(created.value());
    vswitch.set_frame_gate(&supervisor);
    const wirelab::Endpoint switch_endpoint("127.0.0.1", vswitch.port());

    std::thread dataplane([&vswitch] { (void)vswitch.start(); });

    auto client_a = make_client();
    auto client_b = make_client();
    const auto storm = broadcast_frame(0x0a);
    const auto quiet = broadcast_frame(0x0b);

    // client-a speaks first, so it takes ingress port 0; client-b joins so that
    // there is somewhere for a broadcast to be forwarded to.
    ASSERT_TRUE(client_a.send_to(storm, switch_endpoint));
    std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(client_b.send_to(quiet, switch_endpoint));
    EXPECT_GE(drain(client_a, 100ms), 1U);

    // A real broadcast storm from client-a: the first frames are forwarded to
    // client-b, and the port is quarantined once the batch is analysed.
    size_t forwarded_before = 0;
    for (int index = 0; index < 12; ++index)
    {
      ASSERT_TRUE(client_a.send_to(storm, switch_endpoint));
      std::this_thread::sleep_for(10ms);
      forwarded_before += drain(client_b, 5ms);
    }
    EXPECT_GT(forwarded_before, 0U);
    ASSERT_TRUE(controller.port_fault("client-a").has_value());
    EXPECT_TRUE(controller.port_fault("client-a")->isolated);

    // While quarantined, real frames from client-a reach nobody.
    (void)drain(client_b, 50ms);
    size_t forwarded_while_quarantined = 0;
    for (int index = 0; index < 6; ++index)
    {
      ASSERT_TRUE(client_a.send_to(storm, switch_endpoint));
      forwarded_while_quarantined += drain(client_b, 20ms);
    }
    EXPECT_EQ(forwarded_while_quarantined, 0U);

    // Silence lets the lease lapse; the port recovers without operator action.
    std::this_thread::sleep_for(600ms);
    EXPECT_FALSE(controller.port_fault("client-a").has_value());
    ASSERT_TRUE(client_a.send_to(storm, switch_endpoint));
    EXPECT_GE(drain(client_b, 200ms), 1U);

    vswitch.stop();
    dataplane.join();
  }
}  // namespace
