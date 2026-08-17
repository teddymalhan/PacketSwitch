#include "wirelab/accelerated_backends.hpp"

#include <memory>

#include "wirelab/packet_analyzer.hpp"
#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#endif

namespace wirelab
{
  namespace
  {
#if defined(WIRELAB_HAS_CUDA) || defined(WIRELAB_HAS_METAL)
    // Both accelerators report the same three transfer/kernel spans, so the
    // adapter that turns an analyzer into a timed backend is written once.
    template<typename Analyzer>
    BenchmarkBackend timed_backend()
    {
      auto analyzer = std::make_unique<Analyzer>();
      auto* selected = analyzer.get();
      return BenchmarkBackend{ std::move(analyzer),
                               [selected]
                               {
                                 const auto timing = selected->last_timing();
                                 return AnalyzerTiming{ timing.host_to_device_ns,
                                                        timing.kernel_ns,
                                                        timing.device_to_host_ns };
                               } };
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
#endif
      return unexpected{ BenchmarkError::UnknownBackend };
    };
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
    if (backend == "metal")
    {
      return true;
    }
#endif
    return false;
  }
}  // namespace wirelab
