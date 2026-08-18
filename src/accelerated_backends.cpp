#include "wirelab/accelerated_backends.hpp"

#include <memory>
#include <type_traits>
#include <utility>

#include "wirelab/packet_analyzer.hpp"
#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#include "wirelab/metal_traffic_generator.hpp"
#endif

namespace wirelab
{
  namespace
  {
#if defined(WIRELAB_HAS_CUDA) || defined(WIRELAB_HAS_METAL)
    // Both accelerators report the same three transfer/kernel spans, so the
    // adapter that turns an analyzer into a timed backend is written once.
    template<typename Timing>
    AnalyzerTiming to_analyzer_timing(const Timing& timing)
    {
      return AnalyzerTiming{ timing.host_to_device_ns, timing.kernel_ns, timing.device_to_host_ns };
    }

#ifdef WIRELAB_HAS_METAL
    // The pipelined parser reports two spans the batch-at-a-time parsers cannot:
    // what a submit waited for, and what the caller actually waited for.
    AnalyzerTiming to_analyzer_timing(const MetalStreamParser::Timing& timing)
    {
      return AnalyzerTiming{ timing.host_to_device_ns,
                             timing.kernel_ns,
                             timing.device_to_host_ns,
                             timing.transfer_inclusive_ns,
                             timing.queue_wait_ns };
    }
#endif

    template<typename Analyzer, typename... Arguments>
    BenchmarkBackend timed_backend(Arguments&&... arguments)
    {
      auto analyzer = std::make_unique<Analyzer>(std::forward<Arguments>(arguments)...);
      auto* selected = analyzer.get();
      BenchmarkBackend backend{ std::move(analyzer),
                                [selected] { return to_analyzer_timing(selected->last_timing()); } };
      if constexpr (std::is_base_of_v<StreamingPacketAnalyzer, Analyzer>)
      {
        backend.streaming = selected;
      }
      return backend;
    }
#endif
  }  // namespace

  BenchmarkBackendFactory accelerated_benchmark_backend_factory()
  {
    return
        [cpu = cpu_benchmark_backend_factory()](const BenchmarkConfig& config) -> expected<BenchmarkBackend, BenchmarkError>
    {
      if (config.backend == "cpu")
      {
        return cpu(config);
      }
#ifdef WIRELAB_HAS_CUDA
      if (config.backend == "cuda")
      {
        if (!CudaPacketParser::is_available())
        {
          return unexpected{ BenchmarkError::BackendUnavailable };
        }
        return timed_backend<CudaPacketAnalyzer>();
      }
#endif
#ifdef WIRELAB_HAS_METAL
      if (config.backend == "metal")
      {
        if (!MetalPacketParser::is_available())
        {
          return unexpected{ BenchmarkError::BackendUnavailable };
        }
        return timed_backend<MetalPacketAnalyzer>();
      }
      if (config.backend == "metal-live")
      {
        if (!MetalStreamParser::is_available())
        {
          return unexpected{ BenchmarkError::BackendUnavailable };
        }
        return timed_backend<MetalStreamingAnalyzer>(MetalStreamParser::DEFAULT_PIPELINE_DEPTH);
      }
#endif
      return unexpected{ BenchmarkError::UnknownBackend };
    };
  }

  TrafficSourceFactory accelerated_traffic_source_factory()
  {
    return [cpu = cpu_traffic_source_factory()](
               const BenchmarkConfig& config) -> expected<std::unique_ptr<TrafficBatchSource>, BenchmarkError>
    {
      if (config.generator == "cpu")
      {
        return cpu(config);
      }
#ifdef WIRELAB_HAS_METAL
      if (config.generator == "metal")
      {
        if (!MetalTrafficGenerator::is_available())
        {
          return unexpected{ BenchmarkError::BackendUnavailable };
        }
        return std::unique_ptr<TrafficBatchSource>(std::make_unique<MetalTrafficSource>(config.traffic));
      }
#endif
      return unexpected{ BenchmarkError::UnknownBackend };
    };
  }

  expected<std::unique_ptr<PacketAnalyzer>, BenchmarkError> accelerated_packet_analyzer(std::string_view backend)
  {
    // Routed through the benchmark factory rather than repeating the backend
    // switch, so a backend can never exist for a report and not for the switch.
    BenchmarkConfig config;
    config.backend = std::string(backend);
    auto built = accelerated_benchmark_backend_factory()(config);
    if (!built)
    {
      return unexpected{ built.error() };
    }
    return std::move(built.value().analyzer);
  }

  bool benchmark_backend_is_compiled_in(std::string_view backend) noexcept
  {
    if (backend == "cpu")
    {
      return true;
    }
#ifdef WIRELAB_HAS_CUDA
    if (backend == "cuda")
    {
      return true;
    }
#endif
#ifdef WIRELAB_HAS_METAL
    if (backend == "metal" || backend == "metal-live")
    {
      return true;
    }
#endif
    return false;
  }
}  // namespace wirelab
