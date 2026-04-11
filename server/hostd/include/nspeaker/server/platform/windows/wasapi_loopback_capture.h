#pragma once

#include "nspeaker/audio/capture.h"

namespace nspeaker::server {

class WasapiLoopbackCapture final : public audio::IAudioCapture {
public:
    bool Start() override;
    bool ReadFrame(audio::PcmFrame& out) override;
};

}  // namespace nspeaker::server
