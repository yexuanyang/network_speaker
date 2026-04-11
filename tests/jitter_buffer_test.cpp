#include <gtest/gtest.h>

#include "nspeaker/transport/jitter_buffer.h"

namespace {

nspeaker::transport::AudioPacket MakePacket(std::uint32_t sequence) {
    nspeaker::transport::AudioPacket packet;
    packet.header.sequence = sequence;
    packet.payload = {static_cast<std::uint8_t>(sequence & 0xFFU)};
    return packet;
}

}  // namespace

TEST(JitterBufferTest, RestoresInOrderPlaybackAfterReordering) {
    nspeaker::transport::JitterBuffer jitter(2);
    nspeaker::audio::StreamStats stats;

    EXPECT_TRUE(jitter.Push(MakePacket(1), 0, stats));
    EXPECT_TRUE(jitter.Push(MakePacket(0), 0, stats));

    auto first = jitter.PopNext(0);
    auto second = jitter.PopNext(1);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->header.sequence, 0U);
    EXPECT_EQ(second->header.sequence, 1U);
    EXPECT_EQ(stats.packets_reordered, 1U);
}

TEST(JitterBufferTest, RejectsLatePackets) {
    nspeaker::transport::JitterBuffer jitter(2);
    nspeaker::audio::StreamStats stats;

    EXPECT_FALSE(jitter.Push(MakePacket(2), 10, stats));
    EXPECT_EQ(stats.late_dropped, 1U);
}
