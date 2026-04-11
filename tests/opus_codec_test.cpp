#include <gtest/gtest.h>

#include <cmath>

#include "nspeaker/codec/opus_codec.h"

TEST(OpusCodecTest, EncodesAndDecodesStereoPcm) {
    nspeaker::codec::OpusEncoder encoder;
    nspeaker::codec::OpusDecoder decoder;
    ASSERT_TRUE(encoder.ok());
    ASSERT_TRUE(decoder.ok());

    nspeaker::audio::PcmFrame input;
    input.samples_per_channel = nspeaker::audio::kDefaultFrameSamples;
    input.interleaved.resize(input.samples_per_channel * nspeaker::audio::kDefaultChannels);
    for (std::uint32_t i = 0; i < input.samples_per_channel; ++i) {
        const auto sample = static_cast<float>(std::sin(static_cast<double>(i) / 20.0) * 0.3);
        input.interleaved[2 * i] = sample;
        input.interleaved[2 * i + 1] = sample;
    }

    std::vector<std::uint8_t> encoded;
    ASSERT_TRUE(encoder.Encode(input, encoded));
    ASSERT_FALSE(encoded.empty());

    nspeaker::audio::PcmFrame decoded;
    ASSERT_TRUE(decoder.Decode(encoded, nspeaker::audio::kDefaultFrameSamples, decoded));
    EXPECT_EQ(decoded.samples_per_channel, nspeaker::audio::kDefaultFrameSamples);
    EXPECT_EQ(decoded.interleaved.size(), input.interleaved.size());

    float energy = 0.0F;
    for (float sample : decoded.interleaved) {
        energy += std::abs(sample);
    }
    EXPECT_GT(energy, 1.0F);
}
