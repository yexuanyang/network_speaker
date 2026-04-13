#pragma once

#include <memory>
#include <string>

#include "nspeaker/audio/capture.h"
#include "nspeaker/audio/clock.h"

namespace nspeaker::server {

enum class WasapiLoopbackRole {
    kAuto,
    kMultimedia,
    kConsole,
    kCommunications,
};

class WasapiLoopbackCapture final : public audio::IAudioCapture {
public:
    explicit WasapiLoopbackCapture(
        std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>(),
        WasapiLoopbackRole role = WasapiLoopbackRole::kAuto,
        std::string device_id = {});
    ~WasapiLoopbackCapture() override;

    bool Start() override;
    bool ReadFrame(audio::PcmFrame& out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nspeaker::server
