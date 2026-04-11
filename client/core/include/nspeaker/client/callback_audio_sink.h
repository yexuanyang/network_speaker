#pragma once

#include <functional>
#include <utility>

#include "nspeaker/audio/sink.h"

namespace nspeaker::client {

class CallbackAudioSink final : public audio::IAudioSink {
public:
    using Callback = std::function<bool(const audio::PcmFrame&)>;

    explicit CallbackAudioSink(Callback callback) : callback_(std::move(callback)) {}

    bool SubmitPcm(const audio::PcmFrame& frame) override {
        return callback_ != nullptr ? callback_(frame) : false;
    }

private:
    Callback callback_;
};

}  // namespace nspeaker::client
