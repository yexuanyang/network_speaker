#include <gtest/gtest.h>

#include "nspeaker/transport/audio_packet.h"

TEST(AudioPacketTest, RoundTripsSerializedPacket) {
    nspeaker::transport::AudioPacket packet;
    packet.header.sequence = 42;
    packet.header.capture_ts_us = 123456789;
    packet.header.frame_samples = 480;
    packet.payload = {1, 2, 3, 4, 5};

    const auto bytes = nspeaker::transport::SerializePacket(packet);
    const auto decoded = nspeaker::transport::TryParsePacket(bytes);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.sequence, 42U);
    EXPECT_EQ(decoded->header.capture_ts_us, 123456789U);
    EXPECT_EQ(decoded->header.frame_samples, 480U);
    EXPECT_EQ(decoded->payload, packet.payload);
}

TEST(AudioPacketTest, RejectsInvalidPayloadSize) {
    nspeaker::transport::AudioPacket packet;
    packet.payload = {1, 2, 3};
    auto bytes = nspeaker::transport::SerializePacket(packet);
    bytes.pop_back();

    EXPECT_FALSE(nspeaker::transport::TryParsePacket(bytes).has_value());
}
