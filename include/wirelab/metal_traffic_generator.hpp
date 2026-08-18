#ifndef PROJECT_METAL_TRAFFIC_GENERATOR_HPP_
#define PROJECT_METAL_TRAFFIC_GENERATOR_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "wirelab/benchmark.hpp"
#include "wirelab/traffic_generator.hpp"
#include "wirelab/traffic_source.hpp"

namespace wirelab
{
  // Fills a whole batch of frames in one dispatch, one thread per frame. Frames
  // are derived from (config, sequence) with no state between them, so the GPU
  // owes the same bytes write_traffic_frame() would have written on the host.
  class MetalTrafficGenerator final
  {
   public:
    MetalTrafficGenerator();
    ~MetalTrafficGenerator();
    MetalTrafficGenerator(const MetalTrafficGenerator&) = delete;
    MetalTrafficGenerator& operator=(const MetalTrafficGenerator&) = delete;

    [[nodiscard]] static bool is_available() noexcept;

    // Writes count * config.frame_size bytes into out, frame index i holding the
    // frame for sequence first_sequence + i.
    void generate(const TrafficGeneratorConfig& config, uint64_t first_sequence, size_t count, uint8_t* out) const;

   private:
    struct Impl;
    mutable std::unique_ptr<Impl> impl_;
  };

  class MetalTrafficSource final : public TrafficBatchSource
  {
   public:
    explicit MetalTrafficSource(TrafficGeneratorConfig config) noexcept;

    void fill(uint64_t first_sequence, size_t count, std::vector<std::vector<uint8_t>>& frames) override;

   private:
    TrafficGeneratorConfig config_;
    MetalTrafficGenerator generator_;
    std::vector<uint8_t> batch_;
  };
}  // namespace wirelab

#endif
