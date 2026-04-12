#include "nspeaker/server/udp_audio_sender.h"

#include "nspeaker/transport/audio_packet.h"

namespace nspeaker::server {

UdpAudioSender::UdpAudioSender(std::string host, std::uint16_t port, std::uint32_t stream_id)
    : host_(std::move(host)), port_(port), stream_id_(stream_id) {}

bool UdpAudioSender::Open() {
    return socket_.Open();
}

bool UdpAudioSender::Send(std::uint32_t sequence, const audio::PcmFrame& pcm,
                          std::span<const std::uint8_t> opus) {
    transport::AudioPacket packet;
    packet.header.stream_id = stream_id_;
    packet.header.sequence = sequence;
    packet.header.capture_ts_us = pcm.capture_ts_us;
    packet.header.frame_samples = static_cast<std::uint16_t>(pcm.samples_per_channel);
    packet.payload.assign(opus.begin(), opus.end());
    return socket_.SendTo(host_, port_, transport::SerializePacket(packet));
}

}  // namespace nspeaker::server
