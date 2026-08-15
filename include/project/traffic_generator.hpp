#ifndef PROJECT_TRAFFIC_GENERATOR_HPP_
#define PROJECT_TRAFFIC_GENERATOR_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace project
{
  enum class TrafficScenario
  {
    KnownUnicast,
    Broadcast,
    UnknownUnicast,
    Mixed
  };

  struct TrafficGeneratorConfig
  {
    TrafficScenario scenario = TrafficScenario::Mixed;
    uint64_t seed = 1;
    size_t frame_size = 64;
    uint32_t host_count = 16;
  };

  class DeterministicTrafficGenerator
  {
   public:
    explicit DeterministicTrafficGenerator(TrafficGeneratorConfig config);

    [[nodiscard]] std::vector<uint8_t> next_frame();
    [[nodiscard]] std::vector<std::vector<uint8_t>> generate(size_t count);

   private:
    [[nodiscard]] TrafficScenario next_scenario() noexcept;

    TrafficGeneratorConfig config_;
    uint64_t state_;
    uint64_t sequence_ = 0;
  };
}

#endif
