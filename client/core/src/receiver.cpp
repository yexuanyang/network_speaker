#include "nspeaker/client/receiver.h"

namespace nspeaker::client {

bool Receiver::Bind(std::uint16_t port) {
    return socket_.Bind(port);
}

std::optional<transport::AudioPacket> Receiver::PollOne(
    std::chrono::milliseconds timeout, const std::string& allowed_sender_ipv4) {
    auto datagram = socket_.Receive(timeout);
    if (!datagram.has_value()) {
        return std::nullopt;
    }
    if (!allowed_sender_ipv4.empty() && datagram->peer_address != allowed_sender_ipv4) {
        return std::nullopt;
    }
    return transport::TryParsePacket(datagram->payload);
}

std::uint16_t Receiver::bound_port() const noexcept {
    return socket_.bound_port();
}

}  // namespace nspeaker::client
