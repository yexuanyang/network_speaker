#pragma once

#include "nspeaker/audio/frame.h"

namespace nspeaker::audio {

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;

    virtual bool Start() = 0;
    virtual bool ReadFrame(PcmFrame& out) = 0;
};

}  // namespace nspeaker::audio
