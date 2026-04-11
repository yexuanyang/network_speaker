#pragma once

#include <memory>
#include <string>

#include "nspeaker/audio/capture.h"
#include "nspeaker/audio/clock.h"

typedef struct pa_simple pa_simple;

namespace nspeaker::server {

class PulseMonitorCapture final : public audio::IAudioCapture {
public:
    explicit PulseMonitorCapture(std::string source_name,
                                 std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>());
    ~PulseMonitorCapture() override;

    bool Start() override;
    bool ReadFrame(audio::PcmFrame& out) override;

private:
    [[nodiscard]] std::string ResolveMonitorSourceName();

    std::string source_name_;
    std::shared_ptr<audio::Clock> clock_;
    pa_simple* pa_ = nullptr;
    int error_ = 0;
};

}  // namespace nspeaker::server
