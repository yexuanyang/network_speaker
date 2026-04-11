#pragma once

#include <span>
#include <string>
#include <vector>

#include "nspeaker/audio/frame.h"
#include "nspeaker/transport/udp_socket.h"

namespace nspeaker::server {

class UdpAudioSender {
public:
    UdpAudioSender(std::string host, std::uint16_t port);

    bool Open();
    bool Send(std::uint32_t sequence, const audio::PcmFrame& pcm, std::span<const std::uint8_t> opus);

private:
    std::string host_;
    std::uint16_t port_;
    transport::UdpSocket socket_;
};

}  // namespace nspeaker::server
