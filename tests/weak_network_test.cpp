/// @file weak_network_test.cpp
/// Simulates weak-network conditions (random loss, burst loss, high jitter,
/// reordering) and verifies that PLC/FEC concealment + adaptive jitter buffer
/// produce better audio continuity than a naive hard-skip baseline.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

#include "nspeaker/audio/clock.h"
#include "nspeaker/audio/frame.h"
#include "nspeaker/audio/sink.h"
#include "nspeaker/client/player_pipeline.h"
#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/transport/audio_packet.h"

namespace {

// ── Constants ────────────────────────────────────────────────────────────────

// Audio frame parameters.
constexpr std::uint16_t kFrameSamples = nspeaker::audio::kDefaultFrameSamples;  // 480
constexpr std::uint8_t  kDummyPayloadByte = 0xAB;

// Timing: 10 ms per frame = 10 000 µs.
constexpr std::uint64_t kFrameIntervalUs = 10'000;

// RNG seeds for reproducible test patterns.
constexpr std::uint32_t kDefaultSeed   = 42;
constexpr std::uint32_t kSeed20PctLoss = 123;
constexpr std::uint32_t kSeedCombined  = 77;

// Packet-loss rates.
constexpr double kLossRate10Pct = 0.10;
constexpr double kLossRate20Pct = 0.20;

// Minimum output ratio for the 20 % loss scenario (real + PLC frames).
constexpr double kMinOutputRatio = 0.70;

// Total-packet counts per test category.
constexpr std::uint32_t kSmallTestPackets  = 100;
constexpr std::uint32_t kMediumTestPackets = 200;
constexpr std::uint32_t kLargeTestPackets  = 300;

// Burst-loss parameters.
constexpr std::uint32_t kBurstCount  = 3;
constexpr std::uint32_t kBurstLength = 5;

// Jitter deltas (microseconds).
constexpr std::uint64_t kHighJitterDeltaUs   = 8'000;  // ±8 ms
constexpr std::uint64_t kMediumJitterDeltaUs = 5'000;  // ±5 ms

// Reorder test: how many packets to deliver in pair-wise swapped order.
constexpr std::uint32_t kReorderPacketCount = 12;

// Extreme burst: how many consecutive packets are dropped.
constexpr std::uint32_t kExtremeBurstLossLen = 10;

// Tail-tolerance: max packets that may remain buffered un-drained at end.
constexpr std::uint32_t kTailTolerance = 3;

// ── Test Helpers ─────────────────────────────────────────────────────────────

class MemorySink final : public nspeaker::audio::IAudioSink {
public:
    bool SubmitPcm(const nspeaker::audio::PcmFrame& frame) override {
        frames.push_back(frame);
        return true;
    }

    std::vector<nspeaker::audio::PcmFrame> frames;
};

class ManualClock final : public nspeaker::audio::Clock {
public:
    [[nodiscard]] std::uint64_t NowMicros() const noexcept override { return now_us_; }
    void SetMicros(std::uint64_t us) noexcept { now_us_ = us; }

private:
    std::uint64_t now_us_ = 0;
};

/// Passthrough decoder that tracks PLC/FEC calls and always succeeds.
class TrackingDecoder final : public nspeaker::codec::IAudioDecoder {
public:
    bool Decode(std::span<const std::uint8_t>, std::uint16_t frame_samples,
                nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) *
                                   (size_t)nspeaker::audio::kDefaultChannels,
                               0.0F);
        ++decode_count;
        return true;
    }

    bool DecodePLC(std::uint16_t frame_samples, nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) *
                                   (size_t)nspeaker::audio::kDefaultChannels,
                               0.0F);
        ++plc_count;
        return true;
    }

    bool DecodeFEC(std::span<const std::uint8_t>, std::uint16_t frame_samples,
                   nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) *
                                   (size_t)nspeaker::audio::kDefaultChannels,
                               0.0F);
        ++fec_count;
        return true;
    }

    bool Reset() override {
        ++reset_count;
        return true;
    }

    int decode_count = 0;
    int plc_count = 0;
    int fec_count = 0;
    int reset_count = 0;
};

/// A decoder that always fails PLC/FEC — simulates a pipeline without
/// concealment so we can compare against the PLC-enabled path.
class NoConcealmentDecoder final : public nspeaker::codec::IAudioDecoder {
public:
    bool Decode(std::span<const std::uint8_t>, std::uint16_t frame_samples,
                nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) *
                                   (size_t)nspeaker::audio::kDefaultChannels,
                               0.0F);
        return true;
    }

    bool DecodePLC(std::uint16_t, nspeaker::audio::PcmFrame&) override { return false; }
    bool DecodeFEC(std::span<const std::uint8_t>, std::uint16_t,
                   nspeaker::audio::PcmFrame&) override {
        return false;
    }
    bool Reset() override { return true; }
};

nspeaker::transport::AudioPacket MakePacket(std::uint32_t stream_id, std::uint32_t sequence,
                                            std::uint64_t capture_ts_us = 0,
                                            std::uint16_t frame_samples = kFrameSamples) {
    nspeaker::transport::AudioPacket pkt;
    pkt.header.stream_id = stream_id;
    pkt.header.sequence = sequence;
    pkt.header.capture_ts_us = capture_ts_us;
    pkt.header.frame_samples = frame_samples;
    pkt.payload = {kDummyPayloadByte};
    return pkt;
}

/// Holds aggregated results for comparing two pipelines.
struct SimulationResult {
    std::size_t total_output_frames = 0;
    std::uint32_t packets_lost = 0;
    std::uint32_t plc_concealed = 0;
    std::uint32_t fec_recovered = 0;
    std::uint32_t playback_underruns = 0;
    std::uint32_t expected_sequence = 0;
};

/// Default PipelineConfig for weak-network tests: fast-lock disabled, adaptive
/// buffer enabled, PLC/FEC limits generous.
nspeaker::client::PipelineConfig MakeWeakNetworkConfig() {
    nspeaker::client::PipelineConfig cfg;
    cfg.startup_fast_lock_enabled = false;
    cfg.steady_target_packets = 3;
    cfg.min_steady_packets = 2;
    cfg.max_steady_packets = 8;
    cfg.steady_consecutive_threshold = 4;
    cfg.stale_packet_threshold_ms = 0;
    cfg.late_frame_drop_threshold_ms = 0;
    cfg.max_plc_frames_per_gap = 3;
    return cfg;
}

/// Run a simulation: feed `total_packets` with the given `lost_sequences` removed.
/// `jitter_fn` controls inter-arrival timing (given base interval and rng).
template <typename JitterFn>
SimulationResult
RunSimulation(nspeaker::client::PipelineConfig config,
              std::unique_ptr<nspeaker::codec::IAudioDecoder> decoder, std::uint32_t total_packets,
              const std::vector<std::uint32_t>& lost_sequences, JitterFn jitter_fn) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink, clock, config);

    std::uint64_t time_us = 0;
    std::mt19937 rng(kDefaultSeed);

    for (std::uint32_t seq = 0; seq < total_packets; ++seq) {
        // Skip lost packets.
        if (std::ranges::find(lost_sequences, seq) != lost_sequences.end()) {
            time_us += kFrameIntervalUs;
            continue;
        }

        time_us += jitter_fn(kFrameIntervalUs, rng);
        clock->SetMicros(time_us);
        pipeline.PushPacket(MakePacket(1, seq, 0, kFrameSamples));
        pipeline.DrainReady();
    }

    // Final drain to flush remaining buffer.
    pipeline.DrainReady();

    const auto& stats = pipeline.stats();
    return {
        .total_output_frames = sink->frames.size(),
        .packets_lost = stats.packets_lost,
        .plc_concealed = stats.plc_concealed,
        .fec_recovered = stats.fec_recovered,
        .playback_underruns = stats.playback_underruns,
        .expected_sequence = pipeline.expected_sequence(),
    };
}

/// No jitter — constant interval.
auto NoJitter() {
    return [](std::uint64_t base, std::mt19937&) {
        return base;
    };
}

/// Uniform jitter: base +/- max_delta_us.
auto UniformJitter(std::uint64_t max_delta_us) {
    return [max_delta_us](std::uint64_t base, std::mt19937& rng) -> std::uint64_t {
        std::uniform_int_distribution<std::int64_t> dist(-static_cast<std::int64_t>(max_delta_us),
                                                         static_cast<std::int64_t>(max_delta_us));
        const auto delta = dist(rng);
        const auto result = static_cast<std::int64_t>(base) + delta;
        return static_cast<std::uint64_t>(std::max<std::int64_t>(result, 1));
    };
}

/// Build a sorted list of randomly selected sequences in [0, total).
std::vector<std::uint32_t> RandomLossPattern(std::uint32_t total, double loss_rate,
                                             std::uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::bernoulli_distribution dist(loss_rate);
    std::vector<std::uint32_t> lost;
    for (std::uint32_t i = 0; i < total; ++i) {
        if (dist(rng)) lost.push_back(i);
    }
    return lost;
}

/// Build a burst-loss pattern: `burst_count` bursts of `burst_len` consecutive packets,
/// starting at evenly spaced positions.
std::vector<std::uint32_t> BurstLossPattern(std::uint32_t total, std::uint32_t burst_count,
                                            std::uint32_t burst_len) {
    std::vector<std::uint32_t> lost;
    const auto spacing = total / (burst_count + 1);
    for (std::uint32_t b = 0; b < burst_count; ++b) {
        const auto start = spacing * (b + 1);
        for (std::uint32_t i = 0; i < burst_len && (start + i) < total; ++i) {
            lost.push_back(start + i);
        }
    }
    return lost;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test: Random 10% packet loss — PLC produces more output frames than no-PLC.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, RandomLoss10Pct_PLCProducesMoreFrames) {
    constexpr std::uint32_t kTotal = kMediumTestPackets;
    const auto lost = RandomLossPattern(kTotal, kLossRate10Pct);
    auto config = MakeWeakNetworkConfig();

    auto plc_result =
        RunSimulation(config, std::make_unique<TrackingDecoder>(), kTotal, lost, NoJitter());
    auto noplc_result =
        RunSimulation(config, std::make_unique<NoConcealmentDecoder>(), kTotal, lost, NoJitter());

    // PLC pipeline should generate strictly more output frames (concealment fills gaps).
    EXPECT_GT(plc_result.total_output_frames, noplc_result.total_output_frames)
        << "PLC output=" << plc_result.total_output_frames
        << " vs no-PLC output=" << noplc_result.total_output_frames;

    // PLC should have actually concealed some frames.
    EXPECT_GT(plc_result.plc_concealed, 0U);

    // Pipeline should progress through all packets despite losses.
    EXPECT_EQ(plc_result.expected_sequence, kTotal);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Random 20% packet loss — pipeline still recovers and progresses.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, RandomLoss20Pct_PipelineStillProgresses) {
    constexpr std::uint32_t kTotal = kMediumTestPackets;
    const auto lost = RandomLossPattern(kTotal, kLossRate20Pct, kSeed20PctLoss);
    auto config = MakeWeakNetworkConfig();

    auto result =
        RunSimulation(config, std::make_unique<TrackingDecoder>(), kTotal, lost, NoJitter());

    // Pipeline must reach the end — never stall permanently.
    EXPECT_EQ(result.expected_sequence, kTotal);

    // At 20% loss, PLC should be active.
    EXPECT_GT(result.plc_concealed, 0U);

    // Despite losses, at least kMinOutputRatio of frames should be output (real + PLC).
    const auto min_output = static_cast<std::size_t>(kTotal * kMinOutputRatio);
    EXPECT_GE(result.total_output_frames, min_output);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Burst loss (3 bursts of 5 packets) — PLC covers the first few lost
// frames per gap, then hard-skips the tail.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, BurstLoss_PLCCoversPartialGap) {
    constexpr std::uint32_t kTotal = kMediumTestPackets;
    const auto lost = BurstLossPattern(kTotal, kBurstCount, kBurstLength);
    auto config = MakeWeakNetworkConfig();
    config.max_plc_frames_per_gap = 3;

    auto result =
        RunSimulation(config, std::make_unique<TrackingDecoder>(), kTotal, lost, NoJitter());

    // PLC conceals up to 3 frames per gap; remaining 2 per gap are hard-skipped.
    // With 3 bursts, expect roughly 9 PLC frames (3 × 3), though exact count
    // depends on buffer state at each gap.
    EXPECT_GE(result.plc_concealed, 3U) << "Expected at least one burst's worth of PLC concealment";

    // All 15 lost packets should be counted.
    EXPECT_EQ(result.packets_lost, static_cast<std::uint32_t>(lost.size()));

    // Pipeline reaches the end.
    EXPECT_EQ(result.expected_sequence, kTotal);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: FEC recovery — when the next packet is available, FEC fires before PLC.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, FECRecoveryFiresWhenNextPacketAvailable) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();
    auto decoder = std::make_unique<TrackingDecoder>();
    auto* decoder_ptr = decoder.get();

    auto config = MakeWeakNetworkConfig();
    config.steady_target_packets = 2;
    config.min_steady_packets = 2;
    config.max_steady_packets = 2;
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink, clock, config);

    // Prime with seq 0, 1.
    clock->SetMicros(0);
    pipeline.PushPacket(MakePacket(1, 0));
    clock->SetMicros(kFrameIntervalUs);
    pipeline.PushPacket(MakePacket(1, 1));
    EXPECT_EQ(pipeline.DrainReady(), 2U);

    // Drop seq 2, push seq 3 and 4 — FEC should use seq 3's data to recover seq 2.
    clock->SetMicros(kFrameIntervalUs * 3);
    pipeline.PushPacket(MakePacket(1, 3));
    clock->SetMicros(kFrameIntervalUs * 4);
    pipeline.PushPacket(MakePacket(1, 4));

    const auto drained = pipeline.DrainReady();
    EXPECT_EQ(drained, 3U);  // 1 FEC-concealed + 2 real
    EXPECT_EQ(decoder_ptr->fec_count, 1);
    EXPECT_EQ(pipeline.stats().fec_recovered, 1U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 1U);
    EXPECT_EQ(pipeline.expected_sequence(), 5U);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: High jitter — adaptive buffer expands to absorb variance, reducing
// underruns compared to a fixed-size buffer.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, HighJitter_AdaptiveBufferReducesUnderruns) {
    constexpr std::uint32_t kTotal = kMediumTestPackets;
    std::vector<std::uint32_t> no_loss;

    // Adaptive config: buffer can grow from 2 to 8.
    auto adaptive_config = MakeWeakNetworkConfig();
    adaptive_config.steady_target_packets = 2;
    adaptive_config.min_steady_packets = 2;
    adaptive_config.max_steady_packets = 8;

    // Fixed config: buffer stuck at 2, no adaptive growth.
    auto fixed_config = MakeWeakNetworkConfig();
    fixed_config.steady_target_packets = 2;
    fixed_config.min_steady_packets = 2;
    fixed_config.max_steady_packets = 2;

    // Apply strong jitter: ±8ms on a 10ms base (80% jitter ratio).
    auto jitter = UniformJitter(kHighJitterDeltaUs);

    auto adaptive_result = RunSimulation(adaptive_config, std::make_unique<TrackingDecoder>(),
                                         kTotal, no_loss, jitter);
    auto fixed_result =
        RunSimulation(fixed_config, std::make_unique<TrackingDecoder>(), kTotal, no_loss, jitter);

    // Both pipelines should reach the end (no permanent stall).
    EXPECT_EQ(adaptive_result.expected_sequence, kTotal);
    EXPECT_EQ(fixed_result.expected_sequence, kTotal);

    // Adaptive pipeline should have fewer or equal underruns.
    EXPECT_LE(adaptive_result.playback_underruns, fixed_result.playback_underruns)
        << "adaptive underruns=" << adaptive_result.playback_underruns
        << " vs fixed underruns=" << fixed_result.playback_underruns;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Combined scenario — 10% loss + high jitter, PLC pipeline produces
// more continuous audio than no-PLC pipeline.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, CombinedLossAndJitter_PLCImprovesContinuity) {
    constexpr std::uint32_t kTotal = kLargeTestPackets;
    const auto lost = RandomLossPattern(kTotal, kLossRate10Pct, kSeedCombined);
    auto config = MakeWeakNetworkConfig();
    config.max_plc_frames_per_gap = 3;

    auto jitter = UniformJitter(kMediumJitterDeltaUs);

    auto plc_result =
        RunSimulation(config, std::make_unique<TrackingDecoder>(), kTotal, lost, jitter);
    auto noplc_result =
        RunSimulation(config, std::make_unique<NoConcealmentDecoder>(), kTotal, lost, jitter);

    // PLC pipeline produces more output frames.
    EXPECT_GT(plc_result.total_output_frames, noplc_result.total_output_frames);

    // Both reach (or nearly reach) the end — trailing packets may remain buffered.
    EXPECT_GE(plc_result.expected_sequence, kTotal - kTailTolerance);
    EXPECT_GE(noplc_result.expected_sequence, kTotal - kTailTolerance);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Packet reordering under jitter — out-of-order arrivals should be
// absorbed by the jitter buffer and re-sequenced correctly.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, ReorderedPackets_JitterBufferRestoresOrder) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();

    auto config = MakeWeakNetworkConfig();
    config.steady_target_packets = 3;
    config.min_steady_packets = 3;
    config.max_steady_packets = 3;
    nspeaker::client::PlayerPipeline pipeline(std::make_unique<TrackingDecoder>(), sink, clock,
                                              config);

    // Deliver kReorderPacketCount packets with pair-wise swaps: 1,0, 3,2, 5,4, ...
    for (std::uint32_t i = 0; i < kReorderPacketCount; i += 2) {
        clock->SetMicros((i + 1) * kFrameIntervalUs);
        pipeline.PushPacket(MakePacket(1, i + 1));
        clock->SetMicros((i + 2) * kFrameIntervalUs);
        pipeline.PushPacket(MakePacket(1, i));
        pipeline.DrainReady();
    }

    // Pipeline should decode all packets — reordering should not cause losses.
    EXPECT_EQ(pipeline.expected_sequence(), kReorderPacketCount);
    EXPECT_EQ(pipeline.stats().packets_lost, 0U);
    // Last `target - 1` packets may remain in buffer un-drained.
    EXPECT_GE(sink->frames.size(), kReorderPacketCount - config.steady_target_packets + 1);
    EXPECT_GE(pipeline.stats().packets_reordered, kReorderPacketCount / 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Extreme burst loss (10 consecutive) — pipeline hard-skips after PLC
// limit and resumes on arrival of later packets.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, ExtremeBurstLoss_ResumesAfterHardSkip) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();
    auto decoder = std::make_unique<TrackingDecoder>();
    auto* decoder_ptr = decoder.get();

    auto config = MakeWeakNetworkConfig();
    config.steady_target_packets = 3;
    config.min_steady_packets = 3;
    config.max_steady_packets = 3;
    config.max_plc_frames_per_gap = 3;
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink, clock, config);

    // Prime: seq 0..2.
    for (std::uint32_t i = 0; i < config.steady_target_packets; ++i) {
        clock->SetMicros(i * kFrameIntervalUs);
        pipeline.PushPacket(MakePacket(1, i));
    }
    EXPECT_EQ(pipeline.DrainReady(), config.steady_target_packets);

    // Lose kExtremeBurstLossLen consecutive packets, then refill.
    const auto resume_seq = static_cast<std::uint32_t>(config.steady_target_packets) +
                            kExtremeBurstLossLen;
    const auto refill_end = resume_seq + static_cast<std::uint32_t>(config.steady_target_packets);
    for (std::uint32_t i = resume_seq; i < refill_end; ++i) {
        clock->SetMicros(i * kFrameIntervalUs);
        pipeline.PushPacket(MakePacket(1, i));
    }

    const auto drained = pipeline.DrainReady();
    // PLC generates max_plc_frames_per_gap concealment frames, hard-skips the rest.
    EXPECT_GE(drained, config.steady_target_packets);
    EXPECT_EQ(pipeline.stats().plc_concealed,
              static_cast<std::uint32_t>(config.max_plc_frames_per_gap));
    EXPECT_EQ(pipeline.stats().packets_lost, kExtremeBurstLossLen);
    EXPECT_EQ(pipeline.expected_sequence(), refill_end);

    // Decoder should have been reset once (after hard-skip).
    EXPECT_GE(decoder_ptr->reset_count, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: No loss / no jitter baseline — verify zero PLC, zero underruns.
// ─────────────────────────────────────────────────────────────────────────────
TEST(WeakNetworkTest, PerfectNetwork_NoPLCNoUnderruns) {
    constexpr std::uint32_t kTotal = kSmallTestPackets;
    std::vector<std::uint32_t> no_loss;
    auto config = MakeWeakNetworkConfig();

    auto result =
        RunSimulation(config, std::make_unique<TrackingDecoder>(), kTotal, no_loss, NoJitter());

    EXPECT_EQ(result.plc_concealed, 0U);
    EXPECT_EQ(result.fec_recovered, 0U);
    EXPECT_EQ(result.packets_lost, 0U);
    EXPECT_EQ(result.playback_underruns, 0U);
    EXPECT_EQ(result.expected_sequence, kTotal);
    // All packets should produce a decoded frame.
    EXPECT_GE(result.total_output_frames,
              static_cast<std::size_t>(kTotal - config.steady_target_packets));
}
