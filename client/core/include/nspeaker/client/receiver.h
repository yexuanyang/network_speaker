#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "nspeaker/transport/audio_packet.h"
#include "nspeaker/transport/udp_socket.h"

namespace nspeaker::client {

class Receiver {
public:
    bool Bind(std::uint16_t port);
    [[nodiscard]] std::optional<transport::AudioPacket> PollOne(
        std::chrono::milliseconds timeout, const std::string& allowed_sender_ipv4 = {});
    [[nodiscard]] std::uint16_t bound_port() const noexcept;

private:
    transport::UdpSocket socket_;
};

}  // namespace nspeaker::client
