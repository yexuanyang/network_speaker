#pragma once

#include <memory>
#include <optional>
#include <string>

#include "nspeaker/audio/capture.h"
#include "nspeaker/server/platform/windows/wasapi_loopback_capture.h"

namespace nspeaker::server {

struct CaptureConfig {
    std::string source = "sine";
    std::string pulse_source_name;
    double sine_frequency_hz = 440.0;
    std::string wasapi_role = "auto";
    std::string wasapi_device_id;
};

[[nodiscard]] std::unique_ptr<audio::IAudioCapture> CreateCapture(const CaptureConfig& config);
[[nodiscard]] std::optional<WasapiLoopbackRole> ParseWasapiLoopbackRole(const std::string& value);

}  // namespace nspeaker::server
