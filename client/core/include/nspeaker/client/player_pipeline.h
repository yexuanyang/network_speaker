#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "nspeaker/audio/clock.h"
#include "nspeaker/audio/sink.h"
#include "nspeaker/audio/stream_stats.h"
#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/transport/jitter_buffer.h"

namespace nspeaker::client {

struct PipelineConfig {
    bool startup_fast_lock_enabled = true;
    std::size_t startup_buffer_packets = 4;
    std::size_t startup_lead_packets = 2;
    std::size_t steady_target_packets = 3;
    std::size_t steady_consecutive_threshold = 8;
    std::size_t max_catchup_per_drain = 2;
    std::uint32_t late_frame_drop_threshold_ms = 200;
    std::uint32_t stale_packet_threshold_ms = 500;
    std::size_t min_steady_packets = 2;
    std::size_t max_steady_packets = 8;
};

enum class PipelineState { FastLock, Steady };

class PlayerPipeline {
public:
    // Config-based constructor (preferred).
    PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                   std::shared_ptr<audio::IAudioSink> sink, std::shared_ptr<audio::Clock> clock,
                   PipelineConfig config);

    // Legacy constructor — forwards to config-based with default PipelineConfig where
    // steady_target_packets = target_packets and startup_fast_lock_enabled = false
    // (preserves pre-FastLock behaviour for existing call sites and tests).
    PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                   std::shared_ptr<audio::IAudioSink> sink,
                   std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>(),
                   std::size_t target_packets = 6);

    bool PushPacket(transport::AudioPacket packet);
    std::size_t DrainReady();
    [[nodiscard]] std::uint32_t expected_sequence() const noexcept;
    [[nodiscard]] const audio::StreamStats& stats() const noexcept;

private:
    void ResetForStream(std::uint32_t stream_id, std::uint32_t sequence);

    std::unique_ptr<codec::IAudioDecoder> decoder_;
    std::shared_ptr<audio::IAudioSink> sink_;
    std::shared_ptr<audio::Clock> clock_;
    transport::JitterBuffer jitter_;
    PipelineConfig config_;
    PipelineState state_ = PipelineState::FastLock;
    std::size_t steady_consecutive_ = 0;
    audio::StreamStats stats_{};
    std::uint32_t expected_sequence_ = 0;
    std::uint32_t current_stream_id_ = 0;
    bool has_stream_ = false;
    bool primed_ = false;

    // Adaptive jitter buffer management (US3)
    static constexpr std::size_t kVarianceWindowSize = 20;
    std::size_t effective_target_ = 0;
    std::array<std::uint64_t, kVarianceWindowSize> arrival_intervals_{};
    std::size_t arrival_idx_ = 0;
    std::size_t arrival_count_ = 0;
    bool has_first_arrival_ = false;
    std::uint64_t last_arrival_us_ = 0;
    std::uint64_t low_variance_start_us_ = 0;
    std::uint64_t last_target_increase_us_ = 0;
};

}  // namespace nspeaker::client
