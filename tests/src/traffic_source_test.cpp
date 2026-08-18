// The traffic-source seam is what lets a benchmark's frames come from a GPU:
// a batch filled by any source must be the frames write_traffic_frame() would
// have written for the same sequences, which is what these tests pin.

#include "wirelab/traffic_source.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include "wirelab/benchmark.hpp"
#include "wirelab/traffic_generator.hpp"

#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_traffic_generator.hpp"
#endif

#include <gtest/gtest.h>

namespace
{
  wirelab::BenchmarkConfig config_for(wirelab::TrafficScenario scenario, size_t frame_size, uint64_t seed)
  {
    wirelab::BenchmarkConfig config;
    config.traffic.scenario = scenario;
    config.traffic.frame_size = frame_size;
    config.traffic.seed = seed;
    config.packet_count = 1;
    return config;
  }

  std::unique_ptr<wirelab::TrafficBatchSource> cpu_source(const wirelab::BenchmarkConfig& config)
  {
    auto source = wirelab::cpu_traffic_source_factory()(config);
    EXPECT_TRUE(source.has_value());
    return std::move(source.value());
  }
}  // namespace

TEST(TrafficSourceTest, CpuSourceMatchesTheStatelessFrameDerivation)
{
  const auto config = config_for(wirelab::TrafficScenario::Mixed, 128, 7);
  auto source = cpu_source(config);

  std::vector<std::vector<uint8_t>> frames;
  source->fill(0, 16, frames);

  ASSERT_EQ(frames.size(), 16U);
  for (uint64_t sequence = 0; sequence < frames.size(); ++sequence)
  {
    EXPECT_EQ(frames[sequence], wirelab::traffic_frame(config.traffic, sequence)) << "sequence " << sequence;
  }
}

TEST(TrafficSourceTest, CpuSourceStartsAtTheRequestedSequence)
{
  const auto config = config_for(wirelab::TrafficScenario::PortScan, 96, 11);
  auto source = cpu_source(config);

  constexpr uint64_t FIRST_SEQUENCE = 4096;
  std::vector<std::vector<uint8_t>> frames;
  source->fill(FIRST_SEQUENCE, 8, frames);

  ASSERT_EQ(frames.size(), 8U);
  for (size_t index = 0; index < frames.size(); ++index)
  {
    EXPECT_EQ(frames[index], wirelab::traffic_frame(config.traffic, FIRST_SEQUENCE + index)) << "index " << index;
  }
  // A resumed batch must not restart the stream: the first frame here is not the
  // first frame of the run.
  EXPECT_NE(frames.front(), wirelab::traffic_frame(config.traffic, 0));
}

TEST(TrafficSourceTest, ConsecutiveBatchesAreContiguousAndRepeatable)
{
  const auto config = config_for(wirelab::TrafficScenario::UdpFlood, 64, 3);
  auto source = cpu_source(config);

  std::vector<std::vector<uint8_t>> first;
  std::vector<std::vector<uint8_t>> second;
  source->fill(0, 8, first);
  source->fill(8, 8, second);

  std::vector<std::vector<uint8_t>> whole;
  cpu_source(config)->fill(0, 16, whole);

  ASSERT_EQ(whole.size(), 16U);
  for (size_t index = 0; index < 8; ++index)
  {
    EXPECT_EQ(first[index], whole[index]) << "index " << index;
    EXPECT_EQ(second[index], whole[index + 8]) << "index " << index;
  }

  // Refilling the same source over the same sequences must not drift: the frames
  // depend on the sequence, not on how much the source has already produced.
  std::vector<std::vector<uint8_t>> again;
  source->fill(0, 8, again);
  EXPECT_EQ(again, first);
}

TEST(TrafficSourceTest, CpuFactoryRefusesAnUnknownGenerator)
{
  auto config = config_for(wirelab::TrafficScenario::Mixed, 64, 1);
  config.generator = "metal";

  const auto source = wirelab::cpu_traffic_source_factory()(config);
  ASSERT_FALSE(source.has_value());
  EXPECT_EQ(source.error(), wirelab::BenchmarkError::UnknownBackend);
}

#ifdef WIRELAB_HAS_METAL
TEST(MetalTrafficGeneratorTest, GpuFramesEqualCpuFramesAcrossBatches)
{
  if (!wirelab::MetalTrafficGenerator::is_available())
  {
    GTEST_SKIP() << "no compatible Metal device is available";
  }

  constexpr uint64_t FIRST_SEQUENCE = 1000;
  constexpr size_t BATCH_SIZE = 300;
  constexpr size_t BATCH_COUNT = 3;
  const wirelab::TrafficScenario scenarios[] = { wirelab::TrafficScenario::Mixed,
                                                 wirelab::TrafficScenario::BroadcastStorm,
                                                 wirelab::TrafficScenario::PortScan };

  for (const wirelab::TrafficScenario scenario : scenarios)
  {
    const auto config = config_for(scenario, 128, 5);
    wirelab::MetalTrafficSource metal(config.traffic);
    auto cpu = cpu_source(config);

    std::vector<std::vector<uint8_t>> gpu_frames;
    std::vector<std::vector<uint8_t>> cpu_frames;
    for (size_t batch = 0; batch < BATCH_COUNT; ++batch)
    {
      const uint64_t first = FIRST_SEQUENCE + batch * BATCH_SIZE;
      metal.fill(first, BATCH_SIZE, gpu_frames);
      cpu->fill(first, BATCH_SIZE, cpu_frames);

      ASSERT_EQ(gpu_frames.size(), BATCH_SIZE);
      for (size_t index = 0; index < BATCH_SIZE; ++index)
      {
        ASSERT_EQ(gpu_frames[index], cpu_frames[index])
            << "scenario " << wirelab::to_string(scenario) << " sequence " << (first + index);
      }
    }
  }
}

TEST(MetalTrafficGeneratorTest, GpuFramesEqualCpuFramesForATailSizedPayload)
{
  if (!wirelab::MetalTrafficGenerator::is_available())
  {
    GTEST_SKIP() << "no compatible Metal device is available";
  }

  // 45 bytes of payload: not a multiple of the eight-byte random draw, and too
  // small for a UDP header, so both implementations must degrade identically.
  const auto config = config_for(wirelab::TrafficScenario::UdpFlood, 59, 17);
  wirelab::MetalTrafficSource metal(config.traffic);
  auto cpu = cpu_source(config);

  std::vector<std::vector<uint8_t>> gpu_frames;
  std::vector<std::vector<uint8_t>> cpu_frames;
  metal.fill(7, 64, gpu_frames);
  cpu->fill(7, 64, cpu_frames);
  EXPECT_EQ(gpu_frames, cpu_frames);
}
#endif
