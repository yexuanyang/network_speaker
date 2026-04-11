#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "nspeaker/client/receiver.h"
#include "nspeaker/transport/audio_packet.h"
#include "nspeaker/transport/udp_socket.h"

using namespace std::chrono_literals;

namespace {

nspeaker::transport::AudioPacket MakePacket(std::uint32_t sequence) {
    nspeaker::transport::AudioPacket packet;
    packet.header.sequence = sequence;
    packet.header.capture_ts_us = 123456;
    packet.header.frame_samples = 480;
    packet.payload = std::vector<std::uint8_t>{1, 2, 3, 4};
    return packet;
}

}  // namespace

TEST(ReceiverTest, AcceptsAllowedSenderIpv4) {
    nspeaker::client::Receiver receiver;
    ASSERT_TRUE(receiver.Bind(0));

    nspeaker::transport::UdpSocket sender;
    const auto wire = nspeaker::transport::SerializePacket(MakePacket(7));
    ASSERT_TRUE(sender.SendTo("127.0.0.1", receiver.bound_port(), wire));

    auto packet = receiver.PollOne(100ms, "127.0.0.1");
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->header.sequence, 7U);
}

TEST(ReceiverTest, DropsUnexpectedSenderIpv4) {
    nspeaker::client::Receiver receiver;
    ASSERT_TRUE(receiver.Bind(0));

    nspeaker::transport::UdpSocket sender;
    const auto wire = nspeaker::transport::SerializePacket(MakePacket(11));
    ASSERT_TRUE(sender.SendTo("127.0.0.1", receiver.bound_port(), wire));

    EXPECT_FALSE(receiver.PollOne(100ms, "192.0.2.10").has_value());
}
