#include "nspeaker/server/sine_wave_capture.h"

#include <cmath>
#include <numbers>
#include <thread>

namespace nspeaker::server {

SineWaveCapture::SineWaveCapture(double frequency_hz, std::shared_ptr<audio::Clock> clock)
    : frequency_hz_(frequency_hz), clock_(std::move(clock)) {}

bool SineWaveCapture::Start() {
    next_tick_ = std::chrono::steady_clock::now();
    return true;
}

bool SineWaveCapture::ReadFrame(audio::PcmFrame& out) {
    constexpr float amplitude = 0.2F;
    out.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    out.samples_per_channel = audio::kDefaultFrameSamples;
    out.capture_ts_us = clock_->NowMicros();
    out.interleaved.resize(static_cast<std::size_t>(out.samples_per_channel * out.format.channels));

    for (std::uint32_t i = 0; i < out.samples_per_channel; ++i) {
        const auto sample = static_cast<float>(std::sin(phase_) * amplitude);
        out.interleaved[2 * i] = sample;
        out.interleaved[2 * i + 1] = sample;
        phase_ += std::numbers::pi * 2.0 * frequency_hz_ / static_cast<double>(out.format.sample_rate);
        if (phase_ > std::numbers::pi * 2.0) {
            phase_ -= std::numbers::pi * 2.0;
        }
    }

    next_tick_ += std::chrono::milliseconds(10);
    std::this_thread::sleep_until(next_tick_);
    return true;
}

}  // namespace nspeaker::server
