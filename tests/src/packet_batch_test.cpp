#include <array>

#include <gtest/gtest.h>

#include "project/packet_batch.hpp"

namespace
{
  TEST(PacketBatchTest, PacksPacketsIntoContiguousStorageAndRestoresViews)
  {
    const std::array<uint8_t, 3> first = { 1, 2, 3 };
    const std::array<uint8_t, 2> second = { 4, 5 };
    const project::PacketView packets[] = {
      { first.data(), first.size(), 9 },
      { nullptr, 0, 10 },
      { second.data(), second.size(), 11 },
    };

    const auto batch = project::PacketBatch::create(packets, 3, 1234);

    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->timestamp_ns, 1234U);
    EXPECT_EQ(batch->packet_offsets, (std::vector<uint32_t>{ 0, 3, 3, 5 }));
    EXPECT_EQ(batch->packet_lengths, (std::vector<uint16_t>{ 3, 0, 2 }));
    EXPECT_EQ(batch->sender_ids, (std::vector<uint32_t>{ 9, 10, 11 }));
    EXPECT_EQ(batch->packet_bytes, (std::vector<uint8_t>{ 1, 2, 3, 4, 5 }));

    const auto direct_view = batch->packet_view(2);
    EXPECT_EQ(direct_view.bytes[0], 4U);
    EXPECT_EQ(direct_view.ingress_port, 11U);

    const auto views = batch->packet_views();
    ASSERT_EQ(views.size(), 3U);
    EXPECT_EQ(views[0].size, 3U);
    EXPECT_EQ(views[0].bytes[2], 3U);
    EXPECT_EQ(views[1].bytes, nullptr);
    EXPECT_EQ(views[1].size, 0U);
    EXPECT_EQ(views[1].ingress_port, 10U);
    EXPECT_EQ(views[2].bytes[0], 4U);
    EXPECT_EQ(views[2].ingress_port, 11U);
  }

  TEST(PacketBatchTest, RejectsNullOrCudaIncompatiblePackets)
  {
    EXPECT_EQ(project::PacketBatch::create(nullptr, 1).error(), project::PacketBatchError::NullPacketBytes);
    const project::PacketView null_packet = { nullptr, 1, 0 };
    EXPECT_EQ(project::PacketBatch::create(&null_packet, 1).error(), project::PacketBatchError::NullPacketBytes);

    std::vector<uint8_t> oversized(65'536);
    const project::PacketView oversized_packet = { oversized.data(), oversized.size(), 0 };
    EXPECT_EQ(project::PacketBatch::create(&oversized_packet, 1).error(), project::PacketBatchError::PacketTooLarge);
  }
}
