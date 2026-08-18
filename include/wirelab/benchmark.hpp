#ifndef PROJECT_BENCHMARK_HPP_
#define PROJECT_BENCHMARK_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "wirelab/expected.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/traffic_generator.hpp"
#include "wirelab/traffic_source.hpp"

namespace wirelab
{
  enum class BenchmarkError
  {
    UnknownScenario,
    UnknownBackend,
    BackendUnavailable,
    InvalidConfiguration
  };

  [[nodiscard]] const char* to_string(BenchmarkError error) noexcept;

  // A jumbo frame is the largest frame the analyzers and the generator are sized
  // for; anything larger is a configuration mistake rather than a workload.
  constexpr size_t MAX_BENCHMARK_FRAME_SIZE = 9000;

  struct BenchmarkConfig
  {
    TrafficGeneratorConfig traffic;
    size_t packet_count = 100000;
    size_t batch_size = 1;
    std::string backend = "cpu";
    std::string generator = "cpu";
  };

  struct AnalyzerTiming
  {
    uint64_t host_to_device_ns = 0;
    uint64_t kernel_ns = 0;
    uint64_t device_to_host_ns = 0;
    // Submit to result in hand, transfers included. Zero for a backend that
    // answers synchronously, where the batch analysis latency already is it.
    uint64_t transfer_inclusive_ns = 0;
    // Time a submit spent waiting for a free pipeline slot. Non-zero means the
    // GPU is behind the host rather than the other way round.
    uint64_t queue_wait_ns = 0;
  };

  struct BenchmarkResult
  {
    std::string backend;
    std::string scenario;
    uint64_t seed = 0;
    uint64_t frame_size = 0;
    uint64_t batch_size = 0;
    uint64_t total_packets = 0;
    uint64_t completed_packets = 0;
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t malformed_packets = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t known_unicast_packets = 0;
    uint64_t elapsed_ns = 0;
    double packets_per_second = 0.0;
    double goodput_bits_per_second = 0.0;
    double loss_percentage = 0.0;
    uint64_t batch_analysis_latency_p50_ns = 0;
    uint64_t batch_analysis_latency_p95_ns = 0;
    uint64_t batch_analysis_latency_p99_ns = 0;
    AnalyzerTiming timing;
  };

  [[nodiscard]] const char* to_string(TrafficScenario scenario) noexcept;
  [[nodiscard]] expected<TrafficScenario, BenchmarkError> traffic_scenario_from_string(std::string_view name) noexcept;

  // Built by whoever owns the accelerator backends; the core library only knows
  // CPU because it links neither CUDA nor Metal. timing may be empty when the
  // analyzer reports no device transfer or kernel time. streaming is set only
  // when analyzer can also be fed without blocking, and points at that same
  // object rather than owning a second one.
  struct BenchmarkBackend
  {
    std::unique_ptr<PacketAnalyzer> analyzer;
    std::function<AnalyzerTiming()> timing;
    StreamingPacketAnalyzer* streaming = nullptr;
  };

  using BenchmarkBackendFactory = std::function<expected<BenchmarkBackend, BenchmarkError>(const BenchmarkConfig&)>;

  [[nodiscard]] BenchmarkBackendFactory cpu_benchmark_backend_factory();

  // The frames a run analyses. Threaded like the analyzer backend because the
  // same executable owns both: a build without Metal can answer neither.
  using TrafficSourceFactory =
      std::function<expected<std::unique_ptr<TrafficBatchSource>, BenchmarkError>(const BenchmarkConfig&)>;

  [[nodiscard]] TrafficSourceFactory cpu_traffic_source_factory();

  // A benchmark driven in slices: callers that own a thread run it to completion
  // in one advance() call, callers that share a thread with the dataplane hand it
  // a packet budget per tick and still get the counters of an unsliced run.
  class BenchmarkRun
  {
   public:
    [[nodiscard]] static expected<BenchmarkRun, BenchmarkError> create(
        BenchmarkConfig config,
        BenchmarkBackendFactory factory = cpu_benchmark_backend_factory(),
        TrafficSourceFactory traffic = cpu_traffic_source_factory());

    BenchmarkRun(BenchmarkRun&&) noexcept;
    BenchmarkRun& operator=(BenchmarkRun&&) noexcept;
    BenchmarkRun(const BenchmarkRun&) = delete;
    BenchmarkRun& operator=(const BenchmarkRun&) = delete;
    ~BenchmarkRun();

    // Analyses whole batches until max_packets packets have been completed or the
    // run is finished, and returns the packets completed by this call. Only whole
    // batches are ever analysed, so slicing cannot change the measured counters;
    // a call always makes progress, even when max_packets is below batch_size.
    size_t advance(size_t max_packets);
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] size_t completed_packets() const noexcept;
    [[nodiscard]] size_t total_packets() const noexcept;
    // Meaningful at any point; final once finished().
    [[nodiscard]] BenchmarkResult result() const;

   private:
    struct State;

    explicit BenchmarkRun(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
  };
}  // namespace wirelab

#endif
