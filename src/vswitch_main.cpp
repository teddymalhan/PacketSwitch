

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "wirelab/accelerated_backends.hpp"
#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/control_server.hpp"
#include "wirelab/switch_supervisor.hpp"
#include "wirelab/topology.hpp"
#include "wirelab/topology_controller.hpp"
#include "wirelab/vswitch.hpp"

std::unique_ptr<wirelab::VSwitch> g_vswitch;

void signal_handler(int signal)
{
  std::cout << "\n[VSwitch] Received signal " << signal << ", shutting down...\n";
  if (g_vswitch)
  {
    g_vswitch->stop();
    g_vswitch.reset();
  }
  std::exit(0);
}

void setup_signal_handlers()
{
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
}

// Thresholds a lab switch trips on a genuine flood rather than on normal chatter.
wirelab::AnomalyDetectorConfig live_anomaly_config()
{
  wirelab::AnomalyDetectorConfig config;
  config.window_duration_ns = 1'000'000'000;
  config.broadcast_packets_threshold = 100;
  config.unknown_unicast_packets_threshold = 100;
  config.udp_packets_threshold = 200;
  config.port_scan_destinations_threshold = 20;
  config.hot_talker_packets_threshold = 200;
  config.malformed_frames_threshold = 10;
  return config;
}

void install_default_policies(wirelab::AnalysisPipeline& pipeline)
{
  auto& policies = pipeline.policies();
  (void)policies.add_rule(
      { "quarantine-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine });
  (void)policies.add_rule(
      { "quarantine-unknown-unicast-flood", wirelab::AnomalyType::UnknownUnicastFlood, wirelab::PolicyAction::Quarantine });
  (void)policies.add_rule({ "drop-udp-flood", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::Drop });
  (void)policies.add_rule({ "mirror-port-scan", wirelab::AnomalyType::PortScan, wirelab::PolicyAction::Mirror });
}

void print_usage(const char* program_name)
{
  std::cerr << "Usage: " << program_name
            << " <port> [--verbose] [--topology <file>] [--control-port <port>] [--control-address <address>]\n";
  std::cerr << "\n";
  std::cerr << "Arguments:\n";
  std::cerr << "  port     UDP port to listen on (0 for ephemeral)\n";
  std::cerr << "  --verbose  Log every forwarding decision; disabled by default for throughput measurements\n";
  std::cerr << "  --topology <file>  Supervise forwarding with a WireLab topology: frames are analysed,\n";
  std::cerr << "                     anomalies matched against policies, and offending ports contained\n";
  std::cerr << "  --control-port <port>  Serve the WireLab control protocol on this TCP port, so a client can\n";
  std::cerr << "                     drive the switch and watch anomalies, policies and enforcement live.\n";
  std::cerr << "                     Requires --topology, because the supervisor is what serves it\n";
  std::cerr << "  --control-address <address>  Bind the control channel to this address instead of 127.0.0.1.\n";
  std::cerr << "                     The control protocol has no authentication: anyone who can reach the\n";
  std::cerr << "                     address can quarantine a port, so keep it on a network you trust\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << program_name << " 8080\n";
  std::cerr << "  " << program_name << " 8080 --topology topologies/lab.yaml\n";
  std::cerr << "  " << program_name << " 8080 --topology topologies/lab.yaml --control-port 9090\n";
  std::cerr << "\n";
  std::cerr << "The VSwitch will:\n";
  std::cerr << "  - Learn MAC addresses from incoming frames\n";
  std::cerr << "  - Forward unicast frames to known destinations\n";
  std::cerr << "  - Broadcast frames to all known endpoints (except source)\n";
  std::cerr << "  - Discard unknown unicast frames\n";
  std::cerr << "  - Contain a supervised port whose traffic trips a policy, until the lease lapses\n";
}

int main(int argc, char* argv[])
{
  std::cout << "=== VSwitch - Virtual Switch for Layer 2 Networking ===\n\n";

  if (argc < 2)
  {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  wirelab::VSwitchLogLevel log_level = wirelab::VSwitchLogLevel::Lifecycle;
  std::string topology_path;
  long control_port = -1;
  std::string control_address = "127.0.0.1";
  for (int index = 2; index < argc; ++index)
  {
    const std::string_view option(argv[index]);
    if (option == "--verbose")
    {
      log_level = wirelab::VSwitchLogLevel::Frame;
      continue;
    }
    if (option == "--topology" && index + 1 < argc)
    {
      topology_path = argv[++index];
      continue;
    }
    if (option == "--control-port" && index + 1 < argc)
    {
      char* control_end = nullptr;
      control_port = std::strtol(argv[++index], &control_end, 10);
      if (*control_end != '\0' || control_port < 0 || control_port > 65535)
      {
        std::cerr << "Error: Invalid control port '" << argv[index] << "'\n";
        return EXIT_FAILURE;
      }
      continue;
    }
    if (option == "--control-address" && index + 1 < argc)
    {
      control_address = argv[++index];
      continue;
    }
    std::cerr << "Error: Unknown option '" << option << "'\n";
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (control_port >= 0 && topology_path.empty())
  {
    std::cerr << "Error: --control-port requires --topology; the control plane is served by the supervisor\n";
    return EXIT_FAILURE;
  }
  if (control_port < 0 && control_address != "127.0.0.1")
  {
    std::cerr << "Error: --control-address requires --control-port; there is no control channel to bind\n";
    return EXIT_FAILURE;
  }

  const char* port_str = argv[1];

  char* endptr;
  long port_long = std::strtol(port_str, &endptr, 10);

  if (*endptr != '\0' || port_long < 0 || port_long > 65535)
  {
    std::cerr << "Error: Invalid port number '" << port_str << "'\n";
    std::cerr << "Port must be between 0 and 65535.\n";
    return EXIT_FAILURE;
  }

  uint16_t port = static_cast<uint16_t>(port_long);

  std::cout << "Configuration:\n";
  std::cout << "  Port: " << port << (port == 0 ? " (ephemeral)" : "") << "\n";
  std::cout << "  Supervision: " << (topology_path.empty() ? "off" : topology_path) << "\n";
  std::cout << "  Control: "
            << (control_port < 0 ? std::string("off") : control_address + ":" + std::to_string(control_port)) << "\n";
  std::cout << "\n";

  try
  {
    setup_signal_handlers();

    std::cout << "Creating VSwitch...\n";
    auto vswitch_result = wirelab::VSwitch::create(port, log_level);

    if (!vswitch_result)
    {
      std::cerr << "Error: Failed to create VSwitch: " << wirelab::to_string(vswitch_result.error()) << "\n";

      if (vswitch_result.error() == wirelab::VSwitchError::BindFailed)
      {
        std::cerr << "\nHint: Port might be in use. Try a different port number.\n";
        std::cerr << "      Check with: lsof -i :" << port << " or netstat -an | grep " << port << "\n";
      }

      return EXIT_FAILURE;
    }

    g_vswitch = std::make_unique<wirelab::VSwitch>(std::move(*vswitch_result));

    std::cout << "\nVSwitch created successfully!\n";
    std::cout << "  Port: " << g_vswitch->port() << "\n";
    std::cout << "\n";

    wirelab::TopologyController controller;
    wirelab::AnalysisPipeline pipeline(live_anomaly_config());
    std::unique_ptr<wirelab::SwitchSupervisor> supervisor;
    std::optional<wirelab::ControlService> control_service;
    std::optional<wirelab::ControlServer> control_server;
    if (!topology_path.empty())
    {
      auto configuration = wirelab::topology_configuration_from_yaml_file(topology_path);
      if (!configuration)
      {
        std::cerr << "Error: Cannot read topology " << topology_path << ": " << wirelab::to_string(configuration.error())
                  << "\n";
        return EXIT_FAILURE;
      }
      auto topology = wirelab::Topology::create(std::move(configuration.value()));
      if (!topology)
      {
        std::cerr << "Error: Invalid topology " << topology_path << ": " << wirelab::to_string(topology.error()) << "\n";
        return EXIT_FAILURE;
      }
      controller.load(std::move(topology.value()));
      install_default_policies(pipeline);
      supervisor = std::make_unique<wirelab::SwitchSupervisor>(pipeline, controller);
      g_vswitch->set_frame_gate(supervisor.get());
      if (control_port >= 0)
      {
        control_service.emplace(*g_vswitch, controller);
        auto server = wirelab::ControlServer::create(*control_service, static_cast<uint16_t>(control_port), control_address);
        if (!server)
        {
          std::cerr << "Error: Cannot serve the control protocol on port " << control_port << ": "
                    << wirelab::to_string(server.error()) << "\n";
          return EXIT_FAILURE;
        }
        control_server.emplace(std::move(server.value()));
        // The service answers get_supervision_state by asking the supervisor,
        // which is the only thing that knows how much traffic was analysed and
        // which sender took which port.
        control_service->set_supervision_source([&supervisor] { return supervisor->supervision_snapshot(); });
        // A benchmark asked for over the control channel runs on this process,
        // so it must reach the same accelerators the CLI benchmark can.
        control_service->set_benchmark_backends(wirelab::accelerated_benchmark_backend_factory());
        control_service->set_traffic_sources(wirelab::accelerated_traffic_source_factory());
        supervisor->attach_control(*control_server);
        std::cout << "Control channel on " << control_address << ':' << control_server->port()
                  << "; newline-delimited JSON, one control message per line.\n";
        if (control_address != "127.0.0.1")
        {
          std::cout << "  Warning: the control channel is unauthenticated and is not on loopback; anyone who can\n"
                    << "           reach " << control_address << " can fault or quarantine a port.\n";
        }
      }

      std::cout << "Supervising " << controller.port_ids().size() << " ports; senders are bound to them in "
                << "first-seen order.\n";
      for (const auto& rule : pipeline.policies().rules())
      {
        std::cout << "  policy " << rule.name << " -> " << wirelab::to_string(rule.action) << "\n";
      }
      std::cout << "\n";
    }

    std::cout << "Starting frame processing...\n";
    auto start_result = g_vswitch->start();

    if (!start_result)
    {
      std::cerr << "Error: Failed to start VSwitch: " << wirelab::to_string(start_result.error()) << "\n";
      return EXIT_FAILURE;
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  catch (...)
  {
    std::cerr << "Fatal error: Unknown exception occurred\n";
    return EXIT_FAILURE;
  }

  std::cout << "\nVSwitch shut down successfully.\n";
  return EXIT_SUCCESS;
}
