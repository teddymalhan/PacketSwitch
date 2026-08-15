












#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "wirelab/vswitch.hpp"
#include <string_view>



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




void print_usage(const char* program_name)
{
  std::cerr << "Usage: " << program_name << " <port> [--verbose]\n";
  std::cerr << "\n";
  std::cerr << "Arguments:\n";
  std::cerr << "  port     UDP port to listen on (0 for ephemeral)\n";
  std::cerr << "  --verbose  Log every forwarding decision; disabled by default for throughput measurements\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << program_name << " 8080\n";
  std::cerr << "  " << program_name << " 0\n";
  std::cerr << "\n";
  std::cerr << "The VSwitch will:\n";
  std::cerr << "  - Learn MAC addresses from incoming frames\n";
  std::cerr << "  - Forward unicast frames to known destinations\n";
  std::cerr << "  - Broadcast frames to all known endpoints (except source)\n";
  std::cerr << "  - Discard unknown unicast frames\n";
}

int main(int argc, char* argv[])
{
  std::cout << "=== VSwitch - Virtual Switch for Layer 2 Networking ===\n\n";

  
  if (argc < 2 || argc > 3)
  {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  wirelab::VSwitchLogLevel log_level = wirelab::VSwitchLogLevel::Lifecycle;
  if (argc == 3)
  {
    const std::string_view option(argv[2]);
    if (option != "--verbose")
    {
      std::cerr << "Error: Unknown option '" << option << "'\n";
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
    log_level = wirelab::VSwitchLogLevel::Frame;
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
