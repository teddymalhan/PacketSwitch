#include "wirelab/traffic_source.hpp"

#include <memory>

#include "wirelab/benchmark.hpp"
#include "wirelab/traffic_generator.hpp"

namespace wirelab
{
  namespace
  {
    class CpuTrafficSource final : public TrafficBatchSource
    {
     public:
      explicit CpuTrafficSource(TrafficGeneratorConfig config) noexcept : config_(config)
      {
      }

      void fill(uint64_t first_sequence, size_t count, std::vector<std::vector<uint8_t>>& frames) override
      {
        frames.resize(count);
        for (size_t index = 0; index < count; ++index)
        {
          // Sized rather than assigned: a run fills the same vector every batch,
          // so after the first batch this is free.
          frames[index].resize(config_.frame_size);
          write_traffic_frame(config_, first_sequence + index, frames[index].data());
        }
      }

     private:
      TrafficGeneratorConfig config_;
    };
  }  // namespace

  TrafficSourceFactory cpu_traffic_source_factory()
  {
    return [](const BenchmarkConfig& config) -> expected<std::unique_ptr<TrafficBatchSource>, BenchmarkError>
    {
      // The core library links neither CUDA nor Metal, so a GPU generator has to
      // be supplied by an executable that does.
      if (config.generator != "cpu")
      {
        return unexpected{ BenchmarkError::UnknownBackend };
      }
      return std::unique_ptr<TrafficBatchSource>(std::make_unique<CpuTrafficSource>(config.traffic));
    };
  }
}  // namespace wirelab
