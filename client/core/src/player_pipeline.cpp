#include "nspeaker/client/player_pipeline.h"

namespace nspeaker::client {

PlayerPipeline::PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                               std::shared_ptr<audio::IAudioSink> sink,
                               std::shared_ptr<audio::Clock> clock,
                               std::size_t target_packets)
    : decoder_(std::move(decoder)),
      sink_(std::move(sink)),
      clock_(std::move(clock)),
      jitter_(target_packets) {}

bool PlayerPipeline::PushPacket(transport::AudioPacket packet) {
    ++stats_.packets_received;
    return jitter_.Push(std::move(packet), expected_sequence_, stats_);
}

std::size_t PlayerPipeline::DrainReady() {
    if (!primed_) {
        primed_ = jitter_.Primed();
        if (!primed_) {
            return 0;
        }
    }

    std::size_t drained = 0;
    while (true) {
        auto packet = jitter_.PopNext(expected_sequence_);
        if (!packet.has_value()) {
            ++stats_.playback_underruns;
            break;
        }

        nspeaker::audio::PcmFrame pcm;
        if (!decoder_->Decode(packet->payload, packet->header.frame_samples, pcm)) {
            ++stats_.decode_failures;
            ++expected_sequence_;
            continue;
        }

        pcm.capture_ts_us = packet->header.capture_ts_us;
        if (!sink_->SubmitPcm(pcm)) {
            break;
        }

        const auto now_us = clock_->NowMicros();
        if (now_us > pcm.capture_ts_us) {
            stats_.e2e_latency_ms =
                static_cast<std::uint32_t>((now_us - pcm.capture_ts_us) / 1000U);
        }
        stats_.current_jitter_ms =
            static_cast<std::uint32_t>(jitter_.Size() * packet->header.frame_samples / 48U);
        ++expected_sequence_;
        ++drained;
    }
    return drained;
}

std::uint32_t PlayerPipeline::expected_sequence() const noexcept {
    return expected_sequence_;
}

const audio::StreamStats& PlayerPipeline::stats() const noexcept {
    return stats_;
}

}  // namespace nspeaker::client
