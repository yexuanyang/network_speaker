#pragma once

#include <cstdint>
#include <vector>

namespace nspeaker::audio {

inline constexpr int kDefaultSampleRate = 48000;
inline constexpr int kDefaultChannels = 2;
inline constexpr std::uint16_t kDefaultFrameSamples = 480;  // 10 ms @ 48 kHz

struct PcmFormat {
    int sample_rate = kDefaultSampleRate;
    int channels = kDefaultChannels;
};

struct PcmFrame {
    PcmFormat format{};
    std::uint64_t capture_ts_us = 0;
    std::uint32_t samples_per_channel = 0;
    std::vector<float> interleaved;
};

}  // namespace nspeaker::audio
