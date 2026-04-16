#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "nspeaker/audio/capture.h"
#include "nspeaker/audio/clock.h"

typedef struct pa_threaded_mainloop pa_threaded_mainloop;
typedef struct pa_context pa_context;
typedef struct pa_stream pa_stream;

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
    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    std::vector<float> pending_;
    std::chrono::steady_clock::time_point next_tick_{};
    bool started_ = false;
};

}  // namespace nspeaker::server
