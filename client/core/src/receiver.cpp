#include "nspeaker/client/receiver.h"

#include <android/log.h>

namespace nspeaker::client {

namespace {
constexpr char kLogTag[] = "NetworkSpeakerReceiver";
}

bool Receiver::Bind(std::uint16_t port) {
    return socket_.Bind(port);
}

std::optional<transport::AudioPacket> Receiver::PollOne(
    std::chrono::milliseconds timeout, const std::string& allowed_sender_ipv4) {
    auto datagram = socket_.Receive(timeout);
    if (!datagram.has_value()) {
        return std::nullopt;
    }
    
    // Log every received packet for debugging
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "Received packet from %s (allowed=%s, empty=%d)",
                        datagram->peer_address.c_str(),
                        allowed_sender_ipv4.c_str(),
                        allowed_sender_ipv4.empty() ? 1 : 0);
    
    if (!allowed_sender_ipv4.empty() && datagram->peer_address != allowed_sender_ipv4) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "DROPPED packet from %s (expected %s)",
                            datagram->peer_address.c_str(),
                            allowed_sender_ipv4.c_str());
        return std::nullopt;
    }
    return transport::TryParsePacket(datagram->payload);
}

std::uint16_t Receiver::bound_port() const noexcept {
    return socket_.bound_port();
}

}  // namespace nspeaker::client
