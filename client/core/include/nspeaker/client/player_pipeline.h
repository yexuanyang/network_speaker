#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "nspeaker/audio/clock.h"
#include "nspeaker/audio/sink.h"
#include "nspeaker/audio/stream_stats.h"
#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/transport/jitter_buffer.h"

namespace nspeaker::client {

class PlayerPipeline {
public:
    PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                   std::shared_ptr<audio::IAudioSink> sink,
                   std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>(),
                   std::size_t target_packets = 6);

    bool PushPacket(transport::AudioPacket packet);
    std::size_t DrainReady();
    [[nodiscard]] std::uint32_t expected_sequence() const noexcept;
    [[nodiscard]] const audio::StreamStats& stats() const noexcept;

private:
    std::unique_ptr<codec::IAudioDecoder> decoder_;
    std::shared_ptr<audio::IAudioSink> sink_;
    std::shared_ptr<audio::Clock> clock_;
    transport::JitterBuffer jitter_;
    audio::StreamStats stats_{};
    std::uint32_t expected_sequence_ = 0;
    bool primed_ = false;
};

}  // namespace nspeaker::client
