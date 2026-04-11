#pragma once

#include <memory>
#include <string>

#include "nspeaker/audio/capture.h"

namespace nspeaker::server {

struct CaptureConfig {
    std::string source = "sine";
    std::string pulse_source_name;
    double sine_frequency_hz = 440.0;
};

[[nodiscard]] std::unique_ptr<audio::IAudioCapture> CreateCapture(const CaptureConfig& config);

}  // namespace nspeaker::server
