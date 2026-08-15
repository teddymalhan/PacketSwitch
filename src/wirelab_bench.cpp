#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "project/packet_analyzer.hpp"
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
    project::CpuPacketAnalyzer analyzer;
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t malformed_packets = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t known_unicast_packets = 0;
    std::vector<uint64_t> analysis_latency_ns;
    analysis_latency_ns.reserve(packet_count);

    const auto started = std::chrono::steady_clock::now();
    for (size_t index = 0; index < packet_count; ++index)
    {
      const std::vector<uint8_t> bytes = generator.next_frame();
      const project::PacketView packet{ bytes.data(), bytes.size() };
      const auto analysis_started = std::chrono::steady_clock::now();
      const project::AnalysisBatch result = analyzer.analyze(&packet, 1);
      const auto analysis_elapsed = std::chrono::steady_clock::now() - analysis_started;
      analysis_latency_ns.push_back(
          static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(analysis_elapsed).count()));
      received_packets += result.received_packets;
      received_bytes += result.received_bytes;
      malformed_packets += result.malformed_packets;
      broadcast_packets += result.broadcast_packets;
      unknown_unicast_packets += result.unknown_unicast_packets;
      known_unicast_packets += result.known_unicast_packets;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    std::sort(analysis_latency_ns.begin(), analysis_latency_ns.end());
    const auto percentile = [&analysis_latency_ns](size_t numerator, size_t denominator) {
      if (analysis_latency_ns.empty()) return uint64_t{ 0 };
      const size_t index = (analysis_latency_ns.size() * numerator + denominator - 1) / denominator - 1;
      return analysis_latency_ns[index];
    };
    const double loss_percentage =
        received_packets == 0 ? 0.0 : static_cast<double>(malformed_packets) * 100.0 / received_packets;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "backend=cpu packets=" << packet_count << " frame_size=" << config.frame_size << " seed=" << config.seed
              << " elapsed_seconds=" << elapsed_seconds
              << " packets_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : received_packets / elapsed_seconds)
              << " goodput_bits_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : received_bytes * 8.0 / elapsed_seconds)
              << " loss_percentage=" << loss_percentage
              << " analysis_latency_p50_ns=" << percentile(50, 100)
              << " analysis_latency_p95_ns=" << percentile(95, 100)
              << " analysis_latency_p99_ns=" << percentile(99, 100)
              << " malformed_packets=" << malformed_packets
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
