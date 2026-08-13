/**
 * @file vport_main.cpp
 * @brief VPort application - Virtual Port for connecting to VSwitch
 *
 * This application creates a TAP device and connects it to a remote VSwitch
 * via UDP, forwarding Ethernet frames bidirectionally.
 *
 * Usage: vport <vswitch_ip> <vswitch_port> [tap_device_name]
 */

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "project/vport.hpp"

// Global VPort pointer for signal handler
std::unique_ptr<project::VPort> g_vport;

/**
 * @brief Signal handler for graceful shutdown
 *
 * Handles SIGINT (Ctrl+C) and SIGTERM for clean shutdown.
 */
void signal_handler(int signal)
{
  std::cout << "\n[VPort] Received signal " << signal << ", shutting down...\n";
  if (g_vport)
  {
    g_vport->stop();
    g_vport.reset();
  }
  std::exit(0);
}

/**
 * @brief Setup signal handlers for graceful shutdown
 */
void setup_signal_handlers()
{
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
}

/**
 * @brief Print usage information
 */
void print_usage(const char* program_name)
{
  std::cerr << "Usage: " << program_name << " <vswitch_ip> <vswitch_port> [tap_device_name]\n";
  std::cerr << "\n";
  std::cerr << "Arguments:\n";
  std::cerr << "  vswitch_ip        IP address of the VSwitch server\n";
  std::cerr << "  vswitch_port      Port number of the VSwitch server\n";
  std::cerr << "  tap_device_name   Optional TAP device name (default: auto-assigned)\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << program_name << " 127.0.0.1 8080\n";
  std::cerr << "  " << program_name << " 192.168.1.100 9000 tap0\n";
  std::cerr << "\n";
  std::cerr << "Note: This program requires root/sudo privileges to create TAP devices.\n";
}

int main(int argc, char* argv[])
{
  std::cout << "=== VPort - Virtual Port for VSwitch ===\n\n";

  // Parse command-line arguments
  if (argc < 3 || argc > 4)
  {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char* vswitch_ip = argv[1];
  const char* vswitch_port_str = argv[2];
  const char* tap_device_name = (argc == 4) ? argv[3] : "";

  // Parse port number
  char* endptr;
  long port_long = std::strtol(vswitch_port_str, &endptr, 10);

  if (*endptr != '\0' || port_long <= 0 || port_long > 65535)
  {
    std::cerr << "Error: Invalid port number '" << vswitch_port_str << "'\n";
    std::cerr << "Port must be between 1 and 65535.\n";
    return EXIT_FAILURE;
  }

  uint16_t vswitch_port = static_cast<uint16_t>(port_long);

  std::cout << "Configuration:\n";
  std::cout << "  VSwitch Address: " << vswitch_ip << ":" << vswitch_port << "\n";
  std::cout << "  TAP Device: " << (tap_device_name[0] ? tap_device_name : "auto-assign") << "\n";
  std::cout << "\n";

  try
  {
    // Setup signal handlers for graceful shutdown
    setup_signal_handlers();

    // Create VPort instance
    std::cout << "Creating VPort...\n";
    auto vport_result = project::VPort::create(tap_device_name, vswitch_ip, vswitch_port);

    if (!vport_result)
    {
      std::cerr << "Error: Failed to create VPort: " << project::to_string(vport_result.error()) << "\n";

      if (vport_result.error() == project::VPortError::TapDeviceCreationFailed)
      {
        std::cerr << "\nHint: Creating TAP devices requires root privileges.\n";
        std::cerr << "      Try running with sudo: sudo " << argv[0] << " " << vswitch_ip << " " << vswitch_port << "\n";
      }

      return EXIT_FAILURE;
    }

    // Move VPort to global pointer for signal handler
    g_vport = std::make_unique<project::VPort>(std::move(*vport_result));

    std::cout << "\nVPort created successfully!\n";
    std::cout << "  Device: " << g_vport->device_name() << "\n";
    std::cout << "  VSwitch: " << g_vport->vswitch_endpoint() << "\n";
    std::cout << "\n";

    // Start forwarder threads
    std::cout << "Starting forwarder threads...\n";
    auto start_result = g_vport->start();

    if (!start_result)
    {
      std::cerr << "Error: Failed to start VPort: " << project::to_string(start_result.error()) << "\n";
      return EXIT_FAILURE;
    }

    std::cout << "\nVPort is running! Press Ctrl+C to stop.\n";
    std::cout << "===========================================\n\n";

    // Keep the main thread alive
    // The forwarder threads are running in the background
    while (g_vport && g_vport->is_running())
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
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

  std::cout << "\nVPort shut down successfully.\n";
  return EXIT_SUCCESS;
}
