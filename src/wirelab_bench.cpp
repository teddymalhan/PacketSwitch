#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "wirelab/packet_analyzer.hpp"
#include "wirelab/packet_batch.hpp"
#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#endif


#include "wirelab/traffic_generator.hpp"

namespace
{
  wirelab::TrafficScenario parse_scenario(const std::string& value)
  {
    if (value == "known-unicast") return wirelab::TrafficScenario::KnownUnicast;
    if (value == "broadcast") return wirelab::TrafficScenario::Broadcast;
    if (value == "unknown-unicast") return wirelab::TrafficScenario::UnknownUnicast;
    if (value == "mixed-traffic") return wirelab::TrafficScenario::Mixed;
    throw std::invalid_argument("unknown scenario: " + value);
  }

  void print_usage(const char* program)
  {
    std::cerr << "Usage: " << program
              << " [--analyzer cpu|cuda|metal]"
                 " [--scenario known-unicast|broadcast|unknown-unicast|mixed-traffic]"
                 " [--packets count] [--batch-size count] [--frame-size bytes] [--seed value]\n";
  }
}

int main(int argc, char* argv[])
{
  wirelab::TrafficGeneratorConfig config;
  size_t packet_count = 100000;
  size_t batch_size = 1;
  std::string scenario_name = "mixed-traffic";
  std::string analyzer_name = "cpu";

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
      if (option == "--analyzer") analyzer_name = value;
      else if (option == "--scenario")
      {
        config.scenario = parse_scenario(value);
        scenario_name = value;
      }
      else if (option == "--packets") packet_count = std::stoull(value);
      else if (option == "--batch-size") batch_size = std::stoull(value);
      else if (option == "--frame-size") config.frame_size = std::stoull(value);
      else if (option == "--seed") config.seed = std::stoull(value);
      else
      {
        print_usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    if (batch_size == 0)
    {
      throw std::invalid_argument("batch-size must be greater than zero");
    }

    uint64_t host_to_device_ns = 0;
    uint64_t kernel_ns = 0;
    uint64_t device_to_host_ns = 0;
#ifdef WIRELAB_HAS_CUDA
    wirelab::CudaPacketAnalyzer* cuda_analyzer = nullptr;
#endif
#ifdef WIRELAB_HAS_METAL
    wirelab::MetalPacketAnalyzer* metal_analyzer = nullptr;
#endif

    std::unique_ptr<wirelab::PacketAnalyzer> analyzer;
    if (analyzer_name == "cpu")
    {
      analyzer = std::make_unique<wirelab::CpuPacketAnalyzer>();
    }
#ifdef WIRELAB_HAS_CUDA
    else if (analyzer_name == "cuda")
    {
      if (!wirelab::CudaPacketParser::is_available())
      {
        throw std::runtime_error("CUDA analyzer selected, but no compatible CUDA device is available");
      }
      auto selected_cuda_analyzer = std::make_unique<wirelab::CudaPacketAnalyzer>();
      cuda_analyzer = selected_cuda_analyzer.get();
      analyzer = std::move(selected_cuda_analyzer);
    }
#else
    else if (analyzer_name == "cuda")
    {
      throw std::runtime_error(
          "CUDA analyzer selected, but this wirelab_bench build has no CUDA backend; "
          "reconfigure with -DWIRELAB_ENABLE_CUDA=ON");
    }
#endif
#ifdef WIRELAB_HAS_METAL
    else if (analyzer_name == "metal")
    {
      if (!wirelab::MetalPacketParser::is_available())
      {
        throw std::runtime_error("Metal analyzer selected, but no compatible Metal device is available");
      }
      auto selected_metal_analyzer = std::make_unique<wirelab::MetalPacketAnalyzer>();
      metal_analyzer = selected_metal_analyzer.get();
      analyzer = std::move(selected_metal_analyzer);
    }
#else
    else if (analyzer_name == "metal")
    {
      throw std::runtime_error(
          "Metal analyzer selected, but this wirelab_bench build has no Metal backend; "
          "reconfigure with -DWIRELAB_ENABLE_METAL=ON");
    }
#endif
    else
    {
      throw std::invalid_argument("unknown analyzer: " + analyzer_name);
    }

    wirelab::DeterministicTrafficGenerator generator(config);
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t malformed_packets = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t known_unicast_packets = 0;
    std::vector<uint64_t> analysis_latency_ns;
    analysis_latency_ns.reserve((packet_count + batch_size - 1) / batch_size);

    const auto started = std::chrono::steady_clock::now();
    for (size_t generated_packets = 0; generated_packets < packet_count;)
    {
      const size_t current_batch_size = std::min(batch_size, packet_count - generated_packets);
      const auto frames = generator.generate(current_batch_size);
      std::vector<wirelab::PacketView> packets;
      packets.reserve(frames.size());
      for (const auto& frame : frames)
      {
        packets.push_back(wirelab::PacketView{ frame.data(), frame.size() });
      }

      const auto batch = wirelab::PacketBatch::create(packets.data(), packets.size());
      if (!batch)
      {
        throw std::invalid_argument(std::string("cannot create packet batch: ") + wirelab::to_string(batch.error()));
      }

      const auto analysis_started = std::chrono::steady_clock::now();
      const wirelab::AnalysisBatch result = analyzer->analyze(*batch);
      const auto analysis_elapsed = std::chrono::steady_clock::now() - analysis_started;
      analysis_latency_ns.push_back(
          static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(analysis_elapsed).count()));
      received_packets += result.received_packets;
      received_bytes += result.received_bytes;
      malformed_packets += result.malformed_packets;
      broadcast_packets += result.broadcast_packets;
      unknown_unicast_packets += result.unknown_unicast_packets;
      known_unicast_packets += result.known_unicast_packets;
      generated_packets += current_batch_size;
#ifdef WIRELAB_HAS_CUDA
      if (cuda_analyzer != nullptr)
      {
        const auto timing = cuda_analyzer->last_timing();
        host_to_device_ns += timing.host_to_device_ns;
        kernel_ns += timing.kernel_ns;
        device_to_host_ns += timing.device_to_host_ns;
      }
#endif
#ifdef WIRELAB_HAS_METAL
      if (metal_analyzer != nullptr)
      {
        const auto timing = metal_analyzer->last_timing();
        host_to_device_ns += timing.host_to_device_ns;
        kernel_ns += timing.kernel_ns;
        device_to_host_ns += timing.device_to_host_ns;
      }
#endif
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
    std::cout << "backend=" << analyzer_name << " scenario=" << scenario_name << " seed=" << config.seed
              << " frame_size=" << config.frame_size << " packets=" << packet_count << " batch_size=" << batch_size
              << " elapsed_seconds=" << elapsed_seconds
              << " packets_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : received_packets / elapsed_seconds)
              << " goodput_bits_per_second=" << (elapsed_seconds == 0.0 ? 0.0 : received_bytes * 8.0 / elapsed_seconds)
              << " loss_percentage=" << loss_percentage
              << " batch_analysis_latency_p50_ns=" << percentile(50, 100)
              << " batch_analysis_latency_p95_ns=" << percentile(95, 100)
              << " batch_analysis_latency_p99_ns=" << percentile(99, 100)
              << " host_to_device_ns=" << host_to_device_ns << " kernel_ns=" << kernel_ns
              << " device_to_host_ns=" << device_to_host_ns
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
