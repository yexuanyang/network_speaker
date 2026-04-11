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
        nspeaker::audio::PcmFrame frame;
        ASSERT_TRUE(capture.ReadFrame(frame));

        std::vector<std::uint8_t> encoded;
        ASSERT_TRUE(encoder.Encode(frame, encoded));

        nspeaker::transport::AudioPacket outbound;
        outbound.header.sequence = sequence;
        outbound.header.capture_ts_us = frame.capture_ts_us;
        outbound.header.frame_samples = static_cast<std::uint16_t>(frame.samples_per_channel);
        outbound.payload = encoded;

        const auto wire = nspeaker::transport::SerializePacket(outbound);
        auto inbound = nspeaker::transport::TryParsePacket(wire);
        ASSERT_TRUE(inbound.has_value());
        EXPECT_TRUE(pipeline.PushPacket(std::move(*inbound)));
        pipeline.DrainReady();
    }

    EXPECT_GE(sink->frames_.size(), 5U);
    EXPECT_GT(pipeline.stats().e2e_latency_ms, 0U);
}
