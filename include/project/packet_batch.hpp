#ifndef PROJECT_PACKET_BATCH_HPP_
#define PROJECT_PACKET_BATCH_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "project/expected.hpp"
#include "project/packet_analyzer.hpp"

namespace project
{
  enum class PacketBatchError
  {
    NullPacketBytes,
    PacketTooLarge,
    BatchTooLarge
  };

  // Owns the contiguous packet and metadata buffers required by bulk analyzers.
  struct PacketBatch
  {
    std::vector<uint8_t> packet_bytes;
    // packet_offsets has packet_count + 1 entries; the final entry is packet_bytes.size().
    std::vector<uint32_t> packet_offsets;
    std::vector<uint16_t> packet_lengths;
    std::vector<uint32_t> sender_ids;
    uint64_t timestamp_ns = 0;

    [[nodiscard]] static expected<PacketBatch, PacketBatchError> create(const PacketView* packets,
                                                                          size_t packet_count,
                                                                          uint64_t timestamp_ns = 0);
    [[nodiscard]] size_t packet_count() const noexcept;
    [[nodiscard]] std::vector<PacketView> packet_views() const;
  };

  [[nodiscard]] const char* to_string(PacketBatchError error) noexcept;
}

#endif
