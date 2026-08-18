#ifndef PROJECT_TRAFFIC_SOURCE_HPP_
#define PROJECT_TRAFFIC_SOURCE_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wirelab
{
  // Where a benchmark's frames come from. Frames are addressed by absolute
  // sequence rather than pulled one at a time, so a whole batch can be derived
  // in parallel - on a GPU, or on a host resuming an interrupted run - and
  // still be the frames a serial generator would have produced.
  class TrafficBatchSource
  {
   public:
    virtual ~TrafficBatchSource() = default;

    // Fills frames with count frames starting at first_sequence, resizing it to
    // exactly count entries. Implementations reuse the buffers already there.
    virtual void fill(uint64_t first_sequence, size_t count, std::vector<std::vector<uint8_t>>& frames) = 0;
  };
}  // namespace wirelab

#endif
