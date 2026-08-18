#include "wirelab/benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

#include "wirelab/packet_batch.hpp"

namespace wirelab
{
  namespace
  {
    [[nodiscard]] uint64_t elapsed_ns_since(std::chrono::steady_clock::time_point started) noexcept
    {
      const auto elapsed = std::chrono::steady_clock::now() - started;
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }
  }

  struct BenchmarkRun::State
  {
    State(BenchmarkConfig run_config, BenchmarkBackend backend, std::unique_ptr<TrafficBatchSource> traffic)
        : config(std::move(run_config)),
          source(std::move(traffic)),
          analyzer(std::move(backend.analyzer)),
          timing(std::move(backend.timing))
    {
      analysis_latency_ns.reserve((config.packet_count + config.batch_size - 1) / config.batch_size);
    }

    BenchmarkConfig config;
    std::unique_ptr<TrafficBatchSource> source;
    // Reused across batches: a run fills the same buffers packet_count/batch_size
    // times and must not pay for a fresh allocation each time.
    std::vector<std::vector<uint8_t>> frames;
    std::unique_ptr<PacketAnalyzer> analyzer;
    std::function<AnalyzerTiming()> timing;
    std::vector<uint64_t> analysis_latency_ns;
    AnalyzerTiming accumulated_timing;
    size_t completed_packets = 0;
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t malformed_packets = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t known_unicast_packets = 0;
    uint64_t elapsed_ns = 0;
  };

  const char* to_string(BenchmarkError error) noexcept
  {
    switch (error)
    {
      case BenchmarkError::UnknownScenario: return "unknown traffic scenario";
      case BenchmarkError::UnknownBackend: return "unknown benchmark backend";
      case BenchmarkError::BackendUnavailable: return "benchmark backend is unavailable";
      case BenchmarkError::InvalidConfiguration: return "invalid benchmark configuration";
    }
    return "unknown benchmark error";
  }

  const char* to_string(TrafficScenario scenario) noexcept
  {
    switch (scenario)
    {
      case TrafficScenario::KnownUnicast: return "known-unicast";
      case TrafficScenario::Broadcast: return "broadcast";
      case TrafficScenario::UnknownUnicast: return "unknown-unicast";
      case TrafficScenario::Mixed: return "mixed-traffic";
      case TrafficScenario::UdpFlood: return "udp-flood";
      case TrafficScenario::PortScan: return "port-scan";
      case TrafficScenario::BroadcastStorm: return "broadcast-storm";
    }
    return "mixed-traffic";
  }

  expected<TrafficScenario, BenchmarkError> traffic_scenario_from_string(std::string_view name) noexcept
  {
    if (name == "known-unicast") return TrafficScenario::KnownUnicast;
    if (name == "broadcast") return TrafficScenario::Broadcast;
    if (name == "unknown-unicast") return TrafficScenario::UnknownUnicast;
    if (name == "mixed-traffic") return TrafficScenario::Mixed;
    if (name == "udp-flood") return TrafficScenario::UdpFlood;
    if (name == "port-scan") return TrafficScenario::PortScan;
    if (name == "broadcast-storm") return TrafficScenario::BroadcastStorm;
    return unexpected{ BenchmarkError::UnknownScenario };
  }

  BenchmarkBackendFactory cpu_benchmark_backend_factory()
  {
    return [](const BenchmarkConfig& config) -> expected<BenchmarkBackend, BenchmarkError> {
      // The core library links neither CUDA nor Metal, so any accelerator has to
      // be supplied by an executable that does.
      if (config.backend != "cpu")
      {
        return unexpected{ BenchmarkError::UnknownBackend };
      }
      return BenchmarkBackend{ std::make_unique<CpuPacketAnalyzer>(), nullptr };
    };
  }

  expected<BenchmarkRun, BenchmarkError>
  BenchmarkRun::create(BenchmarkConfig config, BenchmarkBackendFactory factory, TrafficSourceFactory traffic)
  {
    if (config.packet_count == 0 || config.batch_size == 0)
    {
      return unexpected{ BenchmarkError::InvalidConfiguration };
    }
    if (config.traffic.frame_size < ETHERNET_HEADER_SIZE || config.traffic.frame_size > MAX_BENCHMARK_FRAME_SIZE)
    {
      return unexpected{ BenchmarkError::InvalidConfiguration };
    }
    // Frames address a source and a destination host, so a scenario needs two.
    if (config.traffic.host_count < 2)
    {
      return unexpected{ BenchmarkError::InvalidConfiguration };
    }
    // PacketBatch addresses its bytes with 32-bit offsets, so a batch that cannot
    // be built is rejected here rather than failing mid-run.
    if (config.batch_size > std::numeric_limits<uint32_t>::max() / config.traffic.frame_size)
    {
      return unexpected{ BenchmarkError::InvalidConfiguration };
    }
    if (!factory || !traffic)
    {
      return unexpected{ BenchmarkError::InvalidConfiguration };
    }

    auto backend = factory(config);
    if (!backend)
    {
      return unexpected{ backend.error() };
    }
    if (!backend->analyzer)
    {
      return unexpected{ BenchmarkError::BackendUnavailable };
    }

    auto source = traffic(config);
    if (!source)
    {
      return unexpected{ source.error() };
    }
    if (!source->get())
    {
      return unexpected{ BenchmarkError::BackendUnavailable };
    }

    return BenchmarkRun(std::make_unique<State>(std::move(config), std::move(*backend), std::move(*source)));
  }

  BenchmarkRun::BenchmarkRun(std::unique_ptr<State> state) noexcept : state_(std::move(state))
  {
  }

  BenchmarkRun::BenchmarkRun(BenchmarkRun&&) noexcept = default;
  BenchmarkRun& BenchmarkRun::operator=(BenchmarkRun&&) noexcept = default;
  BenchmarkRun::~BenchmarkRun() = default;

  size_t BenchmarkRun::advance(size_t max_packets)
  {
    State& state = *state_;
    if (max_packets == 0 || state.completed_packets >= state.config.packet_count)
    {
      return 0;
    }

    // elapsed_ns accumulates only the time spent inside advance(): a run driven one
    // slice per switch tick must report the work it did, not the idle gaps between
    // slices, or its throughput would decay with the caller's scheduling.
    const auto slice_started = std::chrono::steady_clock::now();
    size_t completed_here = 0;
    do
    {
      const size_t remaining = state.config.packet_count - state.completed_packets;
      const size_t current_batch_size = std::min(state.config.batch_size, remaining);
      state.source->fill(state.completed_packets, current_batch_size, state.frames);
      std::vector<PacketView> packets;
      packets.reserve(state.frames.size());
      for (const auto& frame : state.frames)
      {
        packets.push_back(PacketView{ frame.data(), frame.size() });
      }

      const auto batch = PacketBatch::create(packets.data(), packets.size());
      if (!batch)
      {
        // create() bounds the batch bytes, so this is unreachable; finishing the run
        // keeps a caller polling finished() from spinning should that ever change.
        state.completed_packets = state.config.packet_count;
        break;
      }

      const auto analysis_started = std::chrono::steady_clock::now();
      const AnalysisBatch analysis = state.analyzer->analyze(*batch);
      state.analysis_latency_ns.push_back(elapsed_ns_since(analysis_started));

      state.received_packets += analysis.received_packets;
      state.received_bytes += analysis.received_bytes;
      state.malformed_packets += analysis.malformed_packets;
      state.broadcast_packets += analysis.broadcast_packets;
      state.unknown_unicast_packets += analysis.unknown_unicast_packets;
      state.known_unicast_packets += analysis.known_unicast_packets;
      if (state.timing)
      {
        const AnalyzerTiming timing = state.timing();
        state.accumulated_timing.host_to_device_ns += timing.host_to_device_ns;
        state.accumulated_timing.kernel_ns += timing.kernel_ns;
        state.accumulated_timing.device_to_host_ns += timing.device_to_host_ns;
      }

      state.completed_packets += current_batch_size;
      completed_here += current_batch_size;
    } while (completed_here < max_packets && state.completed_packets < state.config.packet_count);

    state.elapsed_ns += elapsed_ns_since(slice_started);
    return completed_here;
  }

  bool BenchmarkRun::finished() const noexcept
  {
    return state_->completed_packets >= state_->config.packet_count;
  }

  size_t BenchmarkRun::completed_packets() const noexcept
  {
    return state_->completed_packets;
  }

  size_t BenchmarkRun::total_packets() const noexcept
  {
    return state_->config.packet_count;
  }

  BenchmarkResult BenchmarkRun::result() const
  {
    const State& state = *state_;

    std::vector<uint64_t> latencies = state.analysis_latency_ns;
    std::sort(latencies.begin(), latencies.end());
    const auto percentile = [&latencies](size_t numerator, size_t denominator) {
      if (latencies.empty()) return uint64_t{ 0 };
      const size_t index = (latencies.size() * numerator + denominator - 1) / denominator - 1;
      return latencies[index];
    };

    const double elapsed_seconds = static_cast<double>(state.elapsed_ns) / 1e9;

    BenchmarkResult result;
    result.backend = state.config.backend;
    result.scenario = to_string(state.config.traffic.scenario);
    result.seed = state.config.traffic.seed;
    result.frame_size = state.config.traffic.frame_size;
    result.batch_size = state.config.batch_size;
    result.total_packets = state.config.packet_count;
    result.completed_packets = state.completed_packets;
    result.received_packets = state.received_packets;
    result.received_bytes = state.received_bytes;
    result.malformed_packets = state.malformed_packets;
    result.broadcast_packets = state.broadcast_packets;
    result.unknown_unicast_packets = state.unknown_unicast_packets;
    result.known_unicast_packets = state.known_unicast_packets;
    result.elapsed_ns = state.elapsed_ns;
    result.packets_per_second = elapsed_seconds == 0.0 ? 0.0 : static_cast<double>(state.received_packets) / elapsed_seconds;
    result.goodput_bits_per_second =
        elapsed_seconds == 0.0 ? 0.0 : static_cast<double>(state.received_bytes) * 8.0 / elapsed_seconds;
    result.loss_percentage =
        state.received_packets == 0 ? 0.0 : static_cast<double>(state.malformed_packets) * 100.0 / state.received_packets;
    result.batch_analysis_latency_p50_ns = percentile(50, 100);
    result.batch_analysis_latency_p95_ns = percentile(95, 100);
    result.batch_analysis_latency_p99_ns = percentile(99, 100);
    result.timing = state.accumulated_timing;
    return result;
  }
}  // namespace wirelab
