#pragma once

#include "nspeaker/audio/frame.h"

namespace nspeaker::audio {

class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    virtual bool SubmitPcm(const PcmFrame& frame) = 0;
};

}  // namespace nspeaker::audio
