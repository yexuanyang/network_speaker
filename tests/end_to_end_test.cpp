#include <gtest/gtest.h>

#include <memory>
#include <span>

#include "nspeaker/audio/clock.h"
#include "nspeaker/audio/sink.h"
#include "nspeaker/client/player_pipeline.h"
#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/server/sine_wave_capture.h"
#include "nspeaker/transport/audio_packet.h"

namespace {

class MemorySink final : public nspeaker::audio::IAudioSink {
public:
    bool SubmitPcm(const nspeaker::audio::PcmFrame& frame) override {
        frames_.push_back(frame);
        return true;
    }

    std::vector<nspeaker::audio::PcmFrame> frames_;
};

class ManualClock final : public nspeaker::audio::Clock {
public:
    [[nodiscard]] std::uint64_t NowMicros() const noexcept override { return now_us_; }

    void SetMicros(std::uint64_t now_us) noexcept { now_us_ = now_us; }

private:
    std::uint64_t now_us_ = 0;
};

class PassthroughDecoder final : public nspeaker::codec::IAudioDecoder {
public:
    bool Decode(std::span<const std::uint8_t>, std::uint16_t frame_samples,
                nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) * 2U, 0.0F);
        return true;
    }

    bool DecodePLC(std::uint16_t frame_samples, nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) * 2U, 0.0F);
        ++plc_count;
        return true;
    }

    bool DecodeFEC(std::span<const std::uint8_t>, std::uint16_t frame_samples,
                   nspeaker::audio::PcmFrame& pcm) override {
        pcm.samples_per_channel = frame_samples;
        pcm.interleaved.assign(static_cast<std::size_t>(frame_samples) * 2U, 0.0F);
        ++fec_count;
        return true;
    }

    bool Reset() override { return true; }

    int plc_count = 0;
    int fec_count = 0;
};

nspeaker::transport::AudioPacket EncodePacket(nspeaker::server::SineWaveCapture& capture,
                                              nspeaker::codec::OpusEncoder& encoder,
                                              std::uint32_t stream_id, std::uint32_t sequence) {
    nspeaker::audio::PcmFrame frame;
    EXPECT_TRUE(capture.ReadFrame(frame));

    std::vector<std::uint8_t> encoded;
    EXPECT_TRUE(encoder.Encode(frame, encoded));

    nspeaker::transport::AudioPacket outbound;
    outbound.header.stream_id = stream_id;
    outbound.header.sequence = sequence;
    outbound.header.capture_ts_us = frame.capture_ts_us;
    outbound.header.frame_samples = static_cast<std::uint16_t>(frame.samples_per_channel);
    outbound.payload = std::move(encoded);
    return outbound;
}

void PushRoundTripPacket(nspeaker::client::PlayerPipeline& pipeline,
                         nspeaker::transport::AudioPacket outbound) {
    const auto wire = nspeaker::transport::SerializePacket(outbound);
    auto inbound = nspeaker::transport::TryParsePacket(wire);
    ASSERT_TRUE(inbound.has_value());
    EXPECT_TRUE(pipeline.PushPacket(std::move(*inbound)));
    pipeline.DrainReady();
}

nspeaker::transport::AudioPacket MakeSyntheticPacket(std::uint32_t stream_id,
                                                     std::uint32_t sequence,
                                                     std::uint64_t capture_ts_us = 0,
                                                     std::uint16_t frame_samples = 480) {
    nspeaker::transport::AudioPacket packet;
    packet.header.stream_id = stream_id;
    packet.header.sequence = sequence;
    packet.header.capture_ts_us = capture_ts_us;
    packet.header.frame_samples = frame_samples;
    packet.payload = {0xAB};
    return packet;
}

}  // namespace

TEST(EndToEndTest, SendsAndReceivesLocalUdpAudio) {
    nspeaker::server::SineWaveCapture capture(880.0);

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink,
                                              std::make_shared<nspeaker::audio::SteadyClock>(), 1);

    // Start() sets next_tick_ = now; call it after Opus init so the 10 ms per-frame
    // sleep in ReadFrame is not eaten up by encoder/decoder initialization time.
    ASSERT_TRUE(capture.Start());

    for (std::uint32_t sequence = 0; sequence < 6; ++sequence) {
        PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 1, sequence));
    }

    EXPECT_GE(sink->frames_.size(), 5U);
    EXPECT_GT(pipeline.stats().e2e_latency_ms, 0U);
}

TEST(EndToEndTest, AcceptsFirstPacketWithNonZeroSequence) {
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink,
                                              std::make_shared<nspeaker::audio::SteadyClock>(), 1);

    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 42, 1000));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 42, 1001));

    EXPECT_GE(sink->frames_.size(), 2U);
    EXPECT_EQ(pipeline.expected_sequence(), 1002U);
}

TEST(EndToEndTest, ResetsPipelineWhenStreamIdChanges) {
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink,
                                              std::make_shared<nspeaker::audio::SteadyClock>(), 1);

    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 100, 0));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 100, 1));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 200, 0));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 200, 1));

    EXPECT_GE(sink->frames_.size(), 4U);
    EXPECT_EQ(pipeline.expected_sequence(), 2U);
}

TEST(EndToEndTest, FastLockJumpsToNewestSequenceOnBacklog) {
    // Build a backlog of 10 packets with FastLock enabled (startup_buffer_packets=4,
    // startup_lead_packets=2, steady_target_packets=3).
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PipelineConfig config;
    config.startup_buffer_packets = 4;
    config.startup_lead_packets = 2;
    config.steady_target_packets = 3;
    config.steady_consecutive_threshold = 8;
    config.stale_packet_threshold_ms = 0;  // disable stale drop for this test
    nspeaker::client::PlayerPipeline pipeline(
        std::move(decoder), sink, std::make_shared<nspeaker::audio::SteadyClock>(), config);

    // Push 10 packets without draining — simulates a backlog.
    constexpr std::uint32_t kBacklog = 10;
    for (std::uint32_t seq = 0; seq < kBacklog; ++seq) {
        const auto wire =
            nspeaker::transport::SerializePacket(EncodePacket(capture, encoder, 1, seq));
        auto inbound = nspeaker::transport::TryParsePacket(wire);
        ASSERT_TRUE(inbound.has_value());
        pipeline.PushPacket(std::move(*inbound));
    }

    // FastLock should fire: newest=9, lead=2, jump_target=7.
    pipeline.DrainReady();

    const auto& stats = pipeline.stats();
    // Skipped packets: 0..6 = 7 packets skipped (jump_target=7, started at 0).
    EXPECT_EQ(stats.startup_skipped_packets, 7U);
    // After draining 7, 8, 9 the next expected should be 10.
    EXPECT_EQ(pipeline.expected_sequence(), 10U);
}

TEST(EndToEndTest, StalePacketDropIsSkippedWhenCaptureTimestampIsZero) {
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 1;
    config.stale_packet_threshold_ms = 1;  // 1 ms threshold — any real packet would be dropped
    nspeaker::client::PlayerPipeline pipeline(
        std::move(decoder), sink, std::make_shared<nspeaker::audio::SteadyClock>(), config);

    // Build a packet with capture_ts_us = 0 (old sender compatibility).
    auto pkt = EncodePacket(capture, encoder, 99, 0);
    pkt.header.capture_ts_us = 0;
    const auto wire = nspeaker::transport::SerializePacket(pkt);
    auto inbound = nspeaker::transport::TryParsePacket(wire);
    ASSERT_TRUE(inbound.has_value());

    // Must NOT be dropped (capture_ts_us == 0 bypasses stale check).
    EXPECT_TRUE(pipeline.PushPacket(std::move(*inbound)));
    EXPECT_EQ(pipeline.stats().late_dropped, 0U);
}

TEST(EndToEndTest, TracksArrivalJitterVarianceFromPacketSpacing) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();

    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 1;
    config.min_steady_packets = 1;
    config.max_steady_packets = 1;
    config.stale_packet_threshold_ms = 0;

    nspeaker::client::PlayerPipeline pipeline(std::make_unique<PassthroughDecoder>(), sink, clock,
                                              config);

    clock->SetMicros(0);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(7, 0)));
    clock->SetMicros(10'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(7, 1)));
    clock->SetMicros(55'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(7, 2)));
    clock->SetMicros(65'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(7, 3)));

    pipeline.DrainReady();

    EXPECT_GT(pipeline.stats().jitter_variance_us, 0U);
}

TEST(EndToEndTest, ExpandsGapRecoveryBufferAfterJitterSpike) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();

    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 2;
    config.min_steady_packets = 2;
    config.max_steady_packets = 4;
    config.steady_consecutive_threshold = 1;
    config.stale_packet_threshold_ms = 0;
    config.late_frame_drop_threshold_ms = 0;

    nspeaker::client::PlayerPipeline pipeline(std::make_unique<PassthroughDecoder>(), sink, clock,
                                              config);

    clock->SetMicros(0);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 0)));
    clock->SetMicros(10'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 1)));
    EXPECT_EQ(pipeline.DrainReady(), 2U);
    EXPECT_EQ(pipeline.expected_sequence(), 2U);

    clock->SetMicros(20'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 2)));
    clock->SetMicros(90'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 3)));
    clock->SetMicros(100'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 4)));
    clock->SetMicros(170'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 5)));
    EXPECT_EQ(pipeline.DrainReady(), 4U);
    EXPECT_EQ(pipeline.expected_sequence(), 6U);

    clock->SetMicros(180'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 7)));
    clock->SetMicros(190'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 8)));

    EXPECT_EQ(pipeline.DrainReady(), 0U);
    EXPECT_EQ(pipeline.expected_sequence(), 6U);
    EXPECT_EQ(pipeline.stats().playback_underruns, 1U);

    clock->SetMicros(200'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 9)));
    clock->SetMicros(210'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(8, 10)));

    EXPECT_EQ(pipeline.DrainReady(), 5U);  // 1 PLC for seq 6 + real 7, 8, 9, 10
    EXPECT_EQ(pipeline.expected_sequence(), 11U);
    EXPECT_EQ(pipeline.stats().packets_lost, 1U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 1U);
}

TEST(EndToEndTest, ShrinksGapRecoveryBufferAfterStableWindow) {
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();

    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 2;
    config.min_steady_packets = 2;
    config.max_steady_packets = 4;
    config.steady_consecutive_threshold = 1;
    config.stale_packet_threshold_ms = 0;
    config.late_frame_drop_threshold_ms = 0;

    nspeaker::client::PlayerPipeline pipeline(std::make_unique<PassthroughDecoder>(), sink, clock,
                                              config);

    clock->SetMicros(0);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 0)));
    clock->SetMicros(10'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 1)));
    EXPECT_EQ(pipeline.DrainReady(), 2U);

    clock->SetMicros(20'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 2)));
    clock->SetMicros(90'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 3)));
    clock->SetMicros(100'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 4)));
    clock->SetMicros(170'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 5)));
    EXPECT_EQ(pipeline.DrainReady(), 4U);
    EXPECT_EQ(pipeline.expected_sequence(), 6U);

    for (std::uint32_t sequence = 6; sequence < 46; ++sequence) {
        clock->SetMicros(170'000 + static_cast<std::uint64_t>(sequence - 5) * 1'000'000U);
        EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, sequence)));
        EXPECT_EQ(pipeline.DrainReady(), 1U);
    }
    EXPECT_EQ(pipeline.expected_sequence(), 46U);

    clock->SetMicros(41'170'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 47)));
    clock->SetMicros(42'170'000);
    EXPECT_TRUE(pipeline.PushPacket(MakeSyntheticPacket(9, 48)));

    EXPECT_EQ(pipeline.DrainReady(), 3U);  // 1 PLC for seq 46 + real 47, 48
    EXPECT_EQ(pipeline.expected_sequence(), 49U);
    EXPECT_EQ(pipeline.stats().packets_lost, 1U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 1U);
}

TEST(EndToEndTest, RecoversAfterSingleLostPacket) {
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink,
                                              std::make_shared<nspeaker::audio::SteadyClock>(), 2);

    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 300, 0));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 300, 1));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 300, 2));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 300, 4));
    PushRoundTripPacket(pipeline, EncodePacket(capture, encoder, 300, 5));

    EXPECT_GE(sink->frames_.size(), 6U);  // 0,1,2 + PLC for 3 + 4,5
    EXPECT_EQ(pipeline.expected_sequence(), 6U);
    EXPECT_EQ(pipeline.stats().packets_lost, 1U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 1U);
}

TEST(EndToEndTest, PLCGeneratesConcealmentForSingleLostPacket) {
    // Verify that a single lost packet produces a PLC concealment frame
    // rather than a hard skip, keeping the audio stream continuous.
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();
    auto decoder = std::make_unique<PassthroughDecoder>();
    auto* decoder_ptr = decoder.get();

    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 2;
    config.min_steady_packets = 2;
    config.max_steady_packets = 2;
    config.stale_packet_threshold_ms = 0;
    config.late_frame_drop_threshold_ms = 0;

    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink, clock, config);

    // Push packets 0, 1 — primes the buffer and drains normally.
    clock->SetMicros(0);
    pipeline.PushPacket(MakeSyntheticPacket(10, 0));
    clock->SetMicros(10'000);
    pipeline.PushPacket(MakeSyntheticPacket(10, 1));
    EXPECT_EQ(pipeline.DrainReady(), 2U);

    // Skip packet 2, push 3 and 4.
    clock->SetMicros(30'000);
    pipeline.PushPacket(MakeSyntheticPacket(10, 3));
    clock->SetMicros(40'000);
    pipeline.PushPacket(MakeSyntheticPacket(10, 4));

    // Buffer has {3, 4}, size=2 >= target=2.  Gap at seq 2.
    // Pipeline should generate PLC for seq 2, then drain 3, 4.
    EXPECT_EQ(pipeline.DrainReady(), 3U);
    EXPECT_EQ(pipeline.expected_sequence(), 5U);
    EXPECT_EQ(pipeline.stats().packets_lost, 1U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 1U);
    // FEC should fire because packet 3 was available when seq 2 was missing.
    EXPECT_EQ(decoder_ptr->fec_count, 1);
}

TEST(EndToEndTest, PLCLimitsConsecutiveConcealmentFrames) {
    // When a burst of packets is lost, PLC should generate at most
    // max_plc_frames_per_gap concealment frames, then hard-skip the rest.
    auto sink = std::make_shared<MemorySink>();
    auto clock = std::make_shared<ManualClock>();

    nspeaker::client::PipelineConfig config;
    config.startup_fast_lock_enabled = false;
    config.steady_target_packets = 2;
    config.min_steady_packets = 2;
    config.max_steady_packets = 2;
    config.stale_packet_threshold_ms = 0;
    config.late_frame_drop_threshold_ms = 0;
    config.max_plc_frames_per_gap = 2;  // only 2 PLC frames allowed

    nspeaker::client::PlayerPipeline pipeline(std::make_unique<PassthroughDecoder>(), sink, clock,
                                              config);

    // Prime with packets 0, 1.
    clock->SetMicros(0);
    pipeline.PushPacket(MakeSyntheticPacket(11, 0));
    clock->SetMicros(10'000);
    pipeline.PushPacket(MakeSyntheticPacket(11, 1));
    EXPECT_EQ(pipeline.DrainReady(), 2U);

    // Lose packets 2, 3, 4, 5, 6 (5-packet burst loss). Push 7, 8.
    clock->SetMicros(70'000);
    pipeline.PushPacket(MakeSyntheticPacket(11, 7));
    clock->SetMicros(80'000);
    pipeline.PushPacket(MakeSyntheticPacket(11, 8));

    // Buffer has {7, 8}, size=2 >= target=2.  Gap 2..6 = 5 missing.
    // PLC generates 2 frames (for seq 2, 3), hard-skips 4, 5, 6, then drains 7, 8.
    const auto drained = pipeline.DrainReady();
    EXPECT_EQ(drained, 4U);  // 2 PLC + 2 real (7, 8)
    EXPECT_EQ(pipeline.expected_sequence(), 9U);
    EXPECT_EQ(pipeline.stats().packets_lost, 5U);
    EXPECT_EQ(pipeline.stats().plc_concealed, 2U);
}

TEST(EndToEndTest, JitterBufferPeekDoesNotConsumePacket) {
    nspeaker::transport::JitterBuffer jitter(2);
    nspeaker::audio::StreamStats stats;

    nspeaker::transport::AudioPacket pkt;
    pkt.header.sequence = 5;
    pkt.payload = {0x01, 0x02};
    jitter.Push(std::move(pkt), 0, stats);

    // Peek should return a valid pointer without removing the packet.
    const auto* peeked = jitter.Peek(5);
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(peeked->header.sequence, 5U);
    EXPECT_EQ(jitter.Size(), 1U);

    // Peek for non-existent sequence returns nullptr.
    EXPECT_EQ(jitter.Peek(99), nullptr);

    // PopNext should still find the packet.
    auto popped = jitter.PopNext(5);
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped->header.sequence, 5U);
    EXPECT_EQ(jitter.Size(), 0U);
}

TEST(EndToEndTest, OpusPLCGeneratesNonSilentConcealment) {
    // Verify that Opus PLC produces a non-trivial concealment frame
    // after decoding real audio data.
    nspeaker::server::SineWaveCapture capture(440.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    nspeaker::codec::OpusDecoder decoder;
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder.ok());

    // Feed a few real frames to build decoder state.
    for (int i = 0; i < 5; ++i) {
        nspeaker::audio::PcmFrame frame;
        ASSERT_TRUE(capture.ReadFrame(frame));
        std::vector<std::uint8_t> encoded;
        ASSERT_TRUE(encoder.Encode(frame, encoded));
        nspeaker::audio::PcmFrame decoded;
        ASSERT_TRUE(decoder.Decode(encoded, 480, decoded));
    }

    // Generate a PLC frame.
    nspeaker::audio::PcmFrame plc_frame;
    ASSERT_TRUE(decoder.DecodePLC(480, plc_frame));
    EXPECT_EQ(plc_frame.samples_per_channel, 480U);
    EXPECT_EQ(plc_frame.interleaved.size(), 960U);

    // PLC output should not be all zeros (it extrapolates from the sine wave).
    float energy = 0.0F;
    for (float sample : plc_frame.interleaved) {
        energy += sample * sample;
    }
    EXPECT_GT(energy, 0.0F);
}
