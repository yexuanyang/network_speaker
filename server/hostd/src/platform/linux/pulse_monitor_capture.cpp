#include "nspeaker/server/platform/linux/pulse_monitor_capture.h"

#ifdef NSPEAKER_HAVE_PULSE
#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/simple.h>
#endif

namespace nspeaker::server {

#ifdef NSPEAKER_HAVE_PULSE
namespace {

struct MonitorLookup {
    std::string default_sink_name;
    std::string monitor_source_name;
    bool server_info_done = false;
    bool sink_info_done = false;
};

void OnSinkInfo(pa_context* context, const pa_sink_info* info, int eol, void* userdata) {
    static_cast<void>(context);
    auto* lookup = static_cast<MonitorLookup*>(userdata);
    if (eol != 0) {
        lookup->sink_info_done = true;
        return;
    }
    if (info != nullptr && info->monitor_source_name != nullptr) {
        lookup->monitor_source_name = info->monitor_source_name;
    }
}

void OnServerInfo(pa_context* context, const pa_server_info* info, void* userdata) {
    auto* lookup = static_cast<MonitorLookup*>(userdata);
    if (info != nullptr && info->default_sink_name != nullptr) {
        lookup->default_sink_name = info->default_sink_name;
        if (pa_operation* op = pa_context_get_sink_info_by_name(
                context, lookup->default_sink_name.c_str(), OnSinkInfo, lookup);
            op != nullptr) {
            pa_operation_unref(op);
        } else {
            lookup->sink_info_done = true;
        }
    } else {
        lookup->sink_info_done = true;
    }
    lookup->server_info_done = true;
}

}  // namespace
#endif

PulseMonitorCapture::PulseMonitorCapture(std::string source_name, std::shared_ptr<audio::Clock> clock)
    : source_name_(std::move(source_name)), clock_(std::move(clock)) {}

PulseMonitorCapture::~PulseMonitorCapture() {
#ifdef NSPEAKER_HAVE_PULSE
    if (pa_ != nullptr) {
        pa_simple_free(pa_);
    }
#endif
}

bool PulseMonitorCapture::Start() {
#ifdef NSPEAKER_HAVE_PULSE
    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = audio::kDefaultSampleRate;
    spec.channels = audio::kDefaultChannels;

    if (source_name_.empty()) {
        source_name_ = ResolveMonitorSourceName();
    }
    const char* device_name = source_name_.empty() ? nullptr : source_name_.c_str();
    pa_ = pa_simple_new(nullptr, "network_speaker", PA_STREAM_RECORD, device_name,
                        "speaker-stream", &spec, nullptr, nullptr, &error_);
    return pa_ != nullptr;
#else
    return false;
#endif
}

bool PulseMonitorCapture::ReadFrame(audio::PcmFrame& out) {
#ifdef NSPEAKER_HAVE_PULSE
    if (pa_ == nullptr) {
        return false;
    }

    out.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    out.samples_per_channel = audio::kDefaultFrameSamples;
    out.capture_ts_us = clock_->NowMicros();
    out.interleaved.resize(static_cast<std::size_t>(out.samples_per_channel * out.format.channels));

    const auto bytes = static_cast<int>(out.interleaved.size() * sizeof(float));
    return pa_simple_read(pa_, out.interleaved.data(), static_cast<std::size_t>(bytes), &error_) >= 0;
#else
    static_cast<void>(out);
    return false;
#endif
}

std::string PulseMonitorCapture::ResolveMonitorSourceName() {
#ifdef NSPEAKER_HAVE_PULSE
    pa_mainloop* mainloop = pa_mainloop_new();
    if (mainloop == nullptr) {
        return {};
    }

    pa_context* context = pa_context_new(pa_mainloop_get_api(mainloop), "network_speaker_lookup");
    if (context == nullptr) {
        pa_mainloop_free(mainloop);
        return {};
    }

    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return {};
    }

    MonitorLookup lookup;
    bool requested_server_info = false;
    bool done = false;
    while (!done) {
        int ret = 0;
        if (pa_mainloop_iterate(mainloop, 1, &ret) < 0) {
            break;
        }

        switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            if (!requested_server_info) {
                if (pa_operation* op = pa_context_get_server_info(context, OnServerInfo, &lookup);
                    op != nullptr) {
                    pa_operation_unref(op);
                    requested_server_info = true;
                } else {
                    done = true;
                }
            }
            if (lookup.server_info_done && lookup.sink_info_done) {
                done = true;
            }
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            done = true;
            break;
        default:
            break;
        }
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(mainloop);
    return lookup.monitor_source_name;
#else
    return {};
#endif
}

}  // namespace nspeaker::server
