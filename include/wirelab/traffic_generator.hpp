#ifndef PROJECT_TRAFFIC_GENERATOR_HPP_
#define PROJECT_TRAFFIC_GENERATOR_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wirelab
{
  enum class TrafficScenario
  {
    KnownUnicast,
    Broadcast,
    UnknownUnicast,
    Mixed,
    UdpFlood,
    PortScan,
    BroadcastStorm
  };

  struct TrafficGeneratorConfig
  {
    TrafficScenario scenario = TrafficScenario::Mixed;
    uint64_t seed = 1;
    size_t frame_size = 64;
    uint32_t host_count = 16;
  };

  // Frames are a pure function of the configuration and a frame counter, so a
  // GPU can produce frame N without having produced frame N - 1, and a resumed
  // run reproduces the same traffic from the counter alone.
  [[nodiscard]] std::vector<uint8_t> traffic_frame(const TrafficGeneratorConfig& config, uint64_t sequence);
  // Returns the bytes written, zero for a configuration that cannot produce a
  // frame. out must have room for config.frame_size bytes.
  size_t write_traffic_frame(const TrafficGeneratorConfig& config, uint64_t sequence, uint8_t* out) noexcept;

  class DeterministicTrafficGenerator
  {
   public:
    explicit DeterministicTrafficGenerator(TrafficGeneratorConfig config);

    [[nodiscard]] std::vector<uint8_t> next_frame();
    [[nodiscard]] std::vector<std::vector<uint8_t>> generate(size_t count);

   private:
    TrafficGeneratorConfig config_;
    uint64_t sequence_ = 0;
  };
}  // namespace wirelab

#endif
