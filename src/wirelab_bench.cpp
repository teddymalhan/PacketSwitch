#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "wirelab/accelerated_backends.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/traffic_generator.hpp"

namespace
{
  wirelab::TrafficScenario parse_scenario(const std::string& value)
  {
    const auto scenario = wirelab::traffic_scenario_from_string(value);
    if (!scenario)
      throw std::invalid_argument("unknown scenario: " + value);
    return *scenario;
  }

  // Reports the backend failure in the terms the operator can act on, which the
  // library-level BenchmarkError deliberately does not encode.
  [[noreturn]] void throw_backend_error(const std::string& analyzer_name, wirelab::BenchmarkError error)
  {
    if (error == wirelab::BenchmarkError::UnknownBackend)
    {
#ifndef WIRELAB_HAS_CUDA
      if (analyzer_name == "cuda")
      {
        throw std::runtime_error(
            "CUDA analyzer selected, but this wirelab_bench build has no CUDA backend; "
            "reconfigure with -DWIRELAB_ENABLE_CUDA=ON");
      }
#endif
#ifndef WIRELAB_HAS_METAL
      if (analyzer_name == "metal")
      {
        throw std::runtime_error(
            "Metal analyzer selected, but this wirelab_bench build has no Metal backend; "
            "reconfigure with -DWIRELAB_ENABLE_METAL=ON");
      }
#endif
      throw std::invalid_argument("unknown analyzer: " + analyzer_name);
    }
    if (error == wirelab::BenchmarkError::BackendUnavailable)
    {
      if (analyzer_name == "cuda")
      {
        throw std::runtime_error("CUDA analyzer selected, but no compatible CUDA device is available");
      }
      throw std::runtime_error("Metal analyzer selected, but no compatible Metal device is available");
    }
    throw std::invalid_argument(wirelab::to_string(error));
  }

  void print_usage(const char* program)
  {
    std::cerr << "Usage: " << program
              << " [--analyzer cpu|cuda|metal]"
                 " [--scenario known-unicast|broadcast|unknown-unicast|mixed-traffic]"
                 " [--packets count] [--batch-size count] [--frame-size bytes] [--seed value]\n";
  }
}  // namespace

int main(int argc, char* argv[])
{
  wirelab::BenchmarkConfig config;

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
      if (option == "--analyzer")
        config.backend = value;
      else if (option == "--scenario")
        config.traffic.scenario = parse_scenario(value);
      else if (option == "--packets")
        config.packet_count = std::stoull(value);
      else if (option == "--batch-size")
        config.batch_size = std::stoull(value);
      else if (option == "--frame-size")
        config.traffic.frame_size = std::stoull(value);
      else if (option == "--seed")
        config.traffic.seed = std::stoull(value);
      else
      {
        print_usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    if (config.batch_size == 0)
    {
      throw std::invalid_argument("batch-size must be greater than zero");
    }

    // Reported here so the operator keeps the frame-level wording the generator
    // used before the benchmark engine validated the whole configuration.
    if (config.traffic.frame_size < wirelab::ETHERNET_HEADER_SIZE)
    {
      throw std::invalid_argument("frame_size must include a complete Ethernet header");
    }
    if (config.traffic.frame_size > wirelab::MAX_BENCHMARK_FRAME_SIZE)
    {
      throw std::invalid_argument("frame_size must not exceed a jumbo frame");
    }

    const std::string analyzer_name = config.backend;
    auto run = wirelab::BenchmarkRun::create(config, wirelab::accelerated_benchmark_backend_factory());
    if (!run)
    {
      throw_backend_error(analyzer_name, run.error());
    }

    while (!run->finished())
    {
      run->advance(run->total_packets());
    }
    const wirelab::BenchmarkResult result = run->result();
    const double elapsed_seconds = static_cast<double>(result.elapsed_ns) / 1e9;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "backend=" << result.backend << " scenario=" << result.scenario << " seed=" << result.seed
              << " frame_size=" << result.frame_size << " packets=" << result.total_packets
              << " batch_size=" << result.batch_size << " elapsed_seconds=" << elapsed_seconds
              << " packets_per_second=" << result.packets_per_second
              << " goodput_bits_per_second=" << result.goodput_bits_per_second
              << " loss_percentage=" << result.loss_percentage
              << " batch_analysis_latency_p50_ns=" << result.batch_analysis_latency_p50_ns
              << " batch_analysis_latency_p95_ns=" << result.batch_analysis_latency_p95_ns
              << " batch_analysis_latency_p99_ns=" << result.batch_analysis_latency_p99_ns
              << " host_to_device_ns=" << result.timing.host_to_device_ns << " kernel_ns=" << result.timing.kernel_ns
              << " device_to_host_ns=" << result.timing.device_to_host_ns
              << " malformed_packets=" << result.malformed_packets << " broadcast_packets=" << result.broadcast_packets
              << " unknown_unicast_packets=" << result.unknown_unicast_packets
              << " known_unicast_packets=" << result.known_unicast_packets << "\n";
  }
  catch (const std::exception& error)
  {
    std::cerr << "wirelab_bench: " << error.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
