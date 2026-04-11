#include "nspeaker/server/capture_factory.h"

#include "nspeaker/server/platform/linux/pulse_monitor_capture.h"
#include "nspeaker/server/platform/windows/wasapi_loopback_capture.h"
#include "nspeaker/server/sine_wave_capture.h"

namespace nspeaker::server {

std::unique_ptr<audio::IAudioCapture> CreateCapture(const CaptureConfig& config) {
    if (config.source == "sine") {
        return std::make_unique<SineWaveCapture>(config.sine_frequency_hz);
    }

#ifdef __linux__
    if (config.source == "pulse") {
        return std::make_unique<PulseMonitorCapture>(config.pulse_source_name);
    }
#endif

#ifdef _WIN32
    if (config.source == "wasapi") {
        return std::make_unique<WasapiLoopbackCapture>();
    }
#endif

    return nullptr;
}

}  // namespace nspeaker::server
