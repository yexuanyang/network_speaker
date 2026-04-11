#pragma once

#include <chrono>
#include <memory>

#include "nspeaker/audio/capture.h"
#include "nspeaker/audio/clock.h"

namespace nspeaker::server {

class SineWaveCapture final : public audio::IAudioCapture {
public:
    explicit SineWaveCapture(double frequency_hz,
                             std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>());

    bool Start() override;
    bool ReadFrame(audio::PcmFrame& out) override;

private:
    double frequency_hz_;
    double phase_ = 0.0;
    std::chrono::steady_clock::time_point next_tick_{};
    std::shared_ptr<audio::Clock> clock_;
};

}  // namespace nspeaker::server
