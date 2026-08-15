#include "wirelab/packet_batch.hpp"

#include <limits>

namespace wirelab
{
  expected<PacketBatch, PacketBatchError> PacketBatch::create(const PacketView* packets, size_t packet_count,
                                                               uint64_t timestamp_ns)
  {
    if (packet_count != 0 && packets == nullptr)
    {
      return unexpected{ PacketBatchError::NullPacketBytes };
    }

    PacketBatch batch;
    batch.timestamp_ns = timestamp_ns;
    batch.packet_offsets.reserve(packet_count + 1);
    batch.packet_lengths.reserve(packet_count);
    batch.sender_ids.reserve(packet_count);
    batch.packet_offsets.push_back(0);

    for (size_t index = 0; index < packet_count; ++index)
    {
      const PacketView& packet = packets[index];
      if (packet.size != 0 && packet.bytes == nullptr)
      {
        return unexpected{ PacketBatchError::NullPacketBytes };
      }
      if (packet.size > std::numeric_limits<uint16_t>::max())
      {
        return unexpected{ PacketBatchError::PacketTooLarge };
      }
      if (packet.size > std::numeric_limits<uint32_t>::max() - batch.packet_bytes.size())
      {
        return unexpected{ PacketBatchError::BatchTooLarge };
      }

      if (packet.size != 0)
      {
        batch.packet_bytes.insert(batch.packet_bytes.end(), packet.bytes, packet.bytes + packet.size);
      }
      batch.packet_lengths.push_back(static_cast<uint16_t>(packet.size));
      batch.sender_ids.push_back(packet.ingress_port);
      batch.packet_offsets.push_back(static_cast<uint32_t>(batch.packet_bytes.size()));
    }
    return batch;
  }

  size_t PacketBatch::packet_count() const noexcept
  {
    return packet_lengths.size();
  }

  PacketView PacketBatch::packet_view(size_t index) const noexcept
  {
    const uint32_t offset = packet_offsets[index];
    return { packet_lengths[index] == 0 ? nullptr : packet_bytes.data() + offset, packet_lengths[index],
             sender_ids[index] };
  }


  std::vector<PacketView> PacketBatch::packet_views() const
  {
    std::vector<PacketView> views;
    views.reserve(packet_count());
    for (size_t index = 0; index < packet_count(); ++index)
    {
      views.push_back(packet_view(index));
    }
    return views;
  }

  const char* to_string(PacketBatchError error) noexcept
  {
    switch (error)
    {
      case PacketBatchError::NullPacketBytes: return "null packet bytes";
      case PacketBatchError::PacketTooLarge: return "packet exceeds CUDA batch length limit";
      case PacketBatchError::BatchTooLarge: return "packet batch exceeds CUDA byte-offset limit";
    }
    return "unknown packet batch error";
  }
}
