#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "project/ethernet_frame.hpp"
#include "project/traffic_generator.hpp"

namespace
{
  project::TrafficScenario parse_scenario(const std::string& value)
  {
    if (value == "known-unicast") return project::TrafficScenario::KnownUnicast;
    if (value == "broadcast") return project::TrafficScenario::Broadcast;
    if (value == "unknown-unicast") return project::TrafficScenario::UnknownUnicast;
    if (value == "mixed-traffic") return project::TrafficScenario::Mixed;
    throw std::invalid_argument("unknown scenario: " + value);
  }

  void print_usage(const char* program)
  {
    std::cerr << "Usage: " << program
              << " [--scenario known-unicast|broadcast|unknown-unicast|mixed-traffic]"
                 " [--packets count] [--frame-size bytes] [--seed value]\n";
  }
}

int main(int argc, char* argv[])
{
  project::TrafficGeneratorConfig config;
  size_t packet_count = 100000;

  try
  {
    for (int index = 1; index < argc; index += 2)
    {
      if (index + 1 >= argc)
      {
        print_usage(argv[0]);
        return EXIT_FAILURE;
      }

      const std::string option = argv[index];
      const std::string value = argv[index + 1];
      if (option == "--scenario") config.scenario = parse_scenario(value);
      else if (option == "--packets") packet_count = std::stoull(value);
      else if (option == "--frame-size") config.frame_size = std::stoull(value);
      else if (option == "--seed") config.seed = std::stoull(value);
      else
      {
        print_usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    project::DeterministicTrafficGenerator generator(config);
    std::unordered_set<project::MacAddress> learned_macs;
    size_t broadcast_packets = 0;
    size_t unknown_unicast_packets = 0;
    size_t known_unicast_packets = 0;

    const auto started = std::chrono::steady_clock::now();
    for (size_t index = 0; index < packet_count; ++index)
    {
      const std::vector<uint8_t> bytes = generator.next_frame();
      const project::EthernetFrame frame = project::EthernetFrame::parse(bytes);
      if (frame.is_broadcast())
      {
        ++broadcast_packets;
      }
      else if (learned_macs.find(frame.dst_mac()) == learned_macs.end())
      {
        ++unknown_unicast_packets;
      }
      else
      {
        ++known_unicast_packets;
      }
      learned_macs.insert(frame.src_mac());
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "backend=cpu packets=" << packet_count << " frame_size=" << config.frame_size << " seed=" << config.seed
              << " elapsed_seconds=" << elapsed_seconds
              << " packets_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : packet_count / elapsed_seconds)
              << " goodput_bits_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : packet_count * config.frame_size * 8.0 / elapsed_seconds)
              << " broadcast_packets=" << broadcast_packets
              << " unknown_unicast_packets=" << unknown_unicast_packets
              << " known_unicast_packets=" << known_unicast_packets << "\n";
  }
  catch (const std::exception& error)
  {
    std::cerr << "wirelab_bench: " << error.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
