#pragma once

#include <memory>

#include "nspeaker/audio/capture.h"
#include "nspeaker/audio/clock.h"

namespace nspeaker::server {

class WasapiLoopbackCapture final : public audio::IAudioCapture {
public:
    explicit WasapiLoopbackCapture(
        std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>());
    ~WasapiLoopbackCapture() override;

    bool Start() override;
    bool ReadFrame(audio::PcmFrame& out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nspeaker::server
