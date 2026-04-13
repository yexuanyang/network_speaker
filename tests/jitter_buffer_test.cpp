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

TEST(JitterBufferTest, NewestSequenceEmptyReturnsNullopt) {
    nspeaker::transport::JitterBuffer jitter(2);
    EXPECT_FALSE(jitter.NewestSequence().has_value());
}

TEST(JitterBufferTest, NewestSequenceReturnsHighestKey) {
    nspeaker::transport::JitterBuffer jitter(4);
    nspeaker::audio::StreamStats stats;

    jitter.Push(MakePacket(10), 0, stats);
    jitter.Push(MakePacket(12), 0, stats);
    jitter.Push(MakePacket(15), 0, stats);

    ASSERT_TRUE(jitter.NewestSequence().has_value());
    EXPECT_EQ(*jitter.NewestSequence(), 15U);
    EXPECT_EQ(*jitter.OldestSequence(), 10U);
}

TEST(JitterBufferTest, DiscardBeforeRemovesOldPackets) {
    nspeaker::transport::JitterBuffer jitter(4);
    nspeaker::audio::StreamStats stats;

    for (std::uint32_t i = 0; i <= 5; ++i) {
        jitter.Push(MakePacket(i), 0, stats);
    }
    EXPECT_EQ(jitter.Size(), 6U);

    jitter.DiscardBefore(3);

    EXPECT_EQ(jitter.Size(), 3U);
    ASSERT_TRUE(jitter.OldestSequence().has_value());
    EXPECT_EQ(*jitter.OldestSequence(), 3U);
    ASSERT_TRUE(jitter.NewestSequence().has_value());
    EXPECT_EQ(*jitter.NewestSequence(), 5U);
}

TEST(JitterBufferTest, DiscardBeforeEmptyBufferIsNoOp) {
    nspeaker::transport::JitterBuffer jitter(2);
    jitter.DiscardBefore(100);  // must not crash
    EXPECT_EQ(jitter.Size(), 0U);
}
