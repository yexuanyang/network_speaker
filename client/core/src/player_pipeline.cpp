#include "nspeaker/client/player_pipeline.h"

#include <algorithm>
#include <limits>

namespace nspeaker::client {

// ── Constructors ─────────────────────────────────────────────────────────────

PlayerPipeline::PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                               std::shared_ptr<audio::IAudioSink> sink,
                               std::shared_ptr<audio::Clock> clock, PipelineConfig config)
    : decoder_(std::move(decoder)), sink_(std::move(sink)), clock_(std::move(clock)),
      jitter_(config.steady_target_packets), config_(config),
      effective_target_(config.steady_target_packets) {}

// Legacy constructor — disables FastLock to preserve pre-optimization behaviour.
PlayerPipeline::PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
                               std::shared_ptr<audio::IAudioSink> sink,
                               std::shared_ptr<audio::Clock> clock, std::size_t target_packets)
    : PlayerPipeline(std::move(decoder), std::move(sink), std::move(clock),
                     PipelineConfig{
                         .startup_fast_lock_enabled = false,
                         .steady_target_packets = target_packets,
                         .late_frame_drop_threshold_ms = 0,  // no soft catch-up
                         .stale_packet_threshold_ms = 0,     // no stale drop
                     }) {}

// ── Private ───────────────────────────────────────────────────────────────────

void PlayerPipeline::ResetForStream(std::uint32_t stream_id, std::uint32_t sequence) {
    current_stream_id_ = stream_id;
    expected_sequence_ = sequence;
    primed_ = false;
    has_stream_ = true;
    state_ = PipelineState::FastLock;
    steady_consecutive_ = 0;
    effective_target_ = config_.steady_target_packets;
    has_first_arrival_ = false;
    last_arrival_us_ = 0;
    arrival_idx_ = 0;
    arrival_count_ = 0;
    arrival_intervals_ = {};
    low_variance_start_us_ = 0;
    last_target_increase_us_ = 0;
    stats_.jitter_variance_us = 0;
    jitter_.Reset();
    decoder_->Reset();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool PlayerPipeline::PushPacket(transport::AudioPacket packet) {
    if (!has_stream_ || packet.header.stream_id != current_stream_id_) {
        ResetForStream(packet.header.stream_id, packet.header.sequence);
    }

    ++stats_.packets_received;

    const auto now_us = clock_->NowMicros();

    // Stale packet drop: discard packets whose audio is too old to be useful.
    if (config_.stale_packet_threshold_ms > 0 && packet.header.capture_ts_us > 0) {
        if (now_us > packet.header.capture_ts_us) {
            const auto age_ms =
                static_cast<std::uint32_t>((now_us - packet.header.capture_ts_us) / 1000U);
            if (age_ms > config_.stale_packet_threshold_ms) {
                ++stats_.late_dropped;
                return false;
            }
        }
    }

    // Track inter-arrival intervals for jitter variance computation (rolling window).
    if (has_first_arrival_) {
        arrival_intervals_[arrival_idx_] = now_us - last_arrival_us_;
        arrival_idx_ = (arrival_idx_ + 1) % kVarianceWindowSize;
        if (arrival_count_ < kVarianceWindowSize) ++arrival_count_;
    }
    has_first_arrival_ = true;
    last_arrival_us_ = now_us;

    if (arrival_count_ >= 2) {
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < arrival_count_; ++i) {
            sum += arrival_intervals_[i];
        }
        const std::uint64_t mean = sum / arrival_count_;
        std::uint64_t var_sum = 0;
        for (std::size_t i = 0; i < arrival_count_; ++i) {
            const std::uint64_t diff = (arrival_intervals_[i] > mean)
                                           ? (arrival_intervals_[i] - mean)
                                           : (mean - arrival_intervals_[i]);
            var_sum += diff * diff;
        }
        stats_.jitter_variance_us = static_cast<std::uint32_t>(
            std::min(var_sum / arrival_count_,
                     static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
    }

    return jitter_.Push(std::move(packet), expected_sequence_, stats_);
}

std::size_t PlayerPipeline::DrainReady() {
    if (!primed_) {
        if (config_.startup_fast_lock_enabled && state_ == PipelineState::FastLock &&
            jitter_.Size() >= config_.startup_buffer_packets) {
            // FastLock: jump expected_sequence forward so we start near the newest audio.
            if (const auto newest = jitter_.NewestSequence();
                newest.has_value() &&
                *newest >
                    expected_sequence_ + static_cast<std::uint32_t>(config_.startup_lead_packets)) {
                const auto jump_target =
                    *newest - static_cast<std::uint32_t>(config_.startup_lead_packets);
                stats_.startup_skipped_packets +=
                    static_cast<std::uint32_t>(jump_target - expected_sequence_);
                expected_sequence_ = jump_target;
            }
            // Discard buffered packets that are now behind the new expected position.
            jitter_.DiscardBefore(expected_sequence_);
            primed_ = true;
        } else {
            primed_ = jitter_.Primed();
            if (!primed_) {
                return 0;
            }
        }
    }

    // Adaptive buffer target adjustment (Steady state only).
    // kHighVarianceUs corresponds to ~5 ms stddev; kLowVarianceUs to ~1 ms stddev.
    static constexpr std::uint32_t kHighVarianceUs = 25'000'000U;
    static constexpr std::uint32_t kLowVarianceUs = 1'000'000U;

    if (state_ == PipelineState::Steady && arrival_count_ >= 2) {
        const auto adapt_now_us = clock_->NowMicros();
        if (stats_.jitter_variance_us > kHighVarianceUs &&
            effective_target_ < config_.max_steady_packets) {
            // Increase target — rate-limited to once per second to avoid runaway growth.
            if (last_target_increase_us_ == 0 ||
                adapt_now_us - last_target_increase_us_ > 1'000'000ULL) {
                ++effective_target_;
                last_target_increase_us_ = adapt_now_us;
                low_variance_start_us_ = 0;
            }
        } else if (stats_.jitter_variance_us < kLowVarianceUs) {
            // Track how long variance has been low; shrink target after 10 s.
            if (low_variance_start_us_ == 0) {
                low_variance_start_us_ = adapt_now_us;
            } else if (adapt_now_us - low_variance_start_us_ >= 10'000'000ULL &&
                       effective_target_ > config_.min_steady_packets) {
                --effective_target_;
                low_variance_start_us_ = 0;
            }
        } else {
            low_variance_start_us_ = 0;
        }
    }

    std::size_t drained = 0;
    std::size_t catchup_this_drain = 0;

    while (true) {
        // Soft catch-up (Steady state only): discard a buffered frame when latency has
        // grown above the threshold.  Limited to max_catchup_per_drain frames per call to
        // avoid jarring audio gaps.
        if (state_ == PipelineState::Steady && config_.late_frame_drop_threshold_ms > 0 &&
            stats_.e2e_latency_ms > config_.late_frame_drop_threshold_ms &&
            catchup_this_drain < config_.max_catchup_per_drain) {
            if (jitter_.PopNext(expected_sequence_).has_value()) {
                ++stats_.packets_lost;
                ++expected_sequence_;
                ++catchup_this_drain;
                continue;
            }
            // No buffered frame to discard; stop trying this drain cycle.
            catchup_this_drain = config_.max_catchup_per_drain;
        }

        auto packet = jitter_.PopNext(expected_sequence_);
        if (!packet.has_value()) {
            // Gap recovery: use effective_target_ (adaptive depth) so the threshold
            // tracks the current network conditions rather than the initial fixed value.
            const auto oldest_sequence = jitter_.OldestSequence();
            if (oldest_sequence.has_value() && *oldest_sequence > expected_sequence_) {
                if (jitter_.Size() >= effective_target_) {
                    stats_.packets_lost += *oldest_sequence - expected_sequence_;
                    expected_sequence_ = *oldest_sequence;
                    continue;
                }
                // Real gap exists but buffer too shallow to skip yet — true underrun.
                ++stats_.playback_underruns;
            }
            // Buffer simply exhausted (no gap) — normal end of buffered data, not an underrun.
            break;
        }

        audio::PcmFrame pcm;
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
        stats_.peak_jitter_ms = std::max(stats_.peak_jitter_ms, stats_.current_jitter_ms);

        // Count consecutive in-order successes toward FastLock → Steady transition.
        if (state_ == PipelineState::FastLock) {
            if (++steady_consecutive_ >= config_.steady_consecutive_threshold) {
                state_ = PipelineState::Steady;
            }
        }

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
