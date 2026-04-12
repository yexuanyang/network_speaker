#include <gtest/gtest.h>

#include <memory>

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

nspeaker::transport::AudioPacket EncodePacket(nspeaker::server::SineWaveCapture& capture,
                                              nspeaker::codec::OpusEncoder& encoder,
                                              std::uint32_t stream_id,
                                              std::uint32_t sequence) {
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

}  // namespace

TEST(EndToEndTest, SendsAndReceivesLocalUdpAudio) {
    nspeaker::server::SineWaveCapture capture(880.0);
    ASSERT_TRUE(capture.Start());

    nspeaker::codec::OpusEncoder encoder;
    auto decoder = std::make_unique<nspeaker::codec::OpusDecoder>();
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder->ok());

    auto sink = std::make_shared<MemorySink>();
    nspeaker::client::PlayerPipeline pipeline(std::move(decoder), sink,
                                              std::make_shared<nspeaker::audio::SteadyClock>(), 1);

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

    EXPECT_GE(sink->frames_.size(), 5U);
    EXPECT_EQ(pipeline.expected_sequence(), 6U);
    EXPECT_EQ(pipeline.stats().packets_lost, 1U);
}
