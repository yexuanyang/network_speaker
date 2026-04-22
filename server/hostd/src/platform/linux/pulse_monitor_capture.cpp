#include "nspeaker/server/platform/linux/pulse_monitor_capture.h"

#include <thread>

#ifdef NSPEAKER_HAVE_PULSE
#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/stream.h>
#include <pulse/thread-mainloop.h>
#endif

namespace nspeaker::server {

#ifdef NSPEAKER_HAVE_PULSE
namespace {

constexpr std::size_t kTargetFloats =
    static_cast<std::size_t>(audio::kDefaultFrameSamples) * audio::kDefaultChannels;
constexpr auto kFrameDuration = std::chrono::milliseconds(10);

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

void OnContextStateChanged(pa_context* /*context*/, void* userdata) {
    pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(userdata), 0);
}

void OnStreamStateChanged(pa_stream* /*stream*/, void* userdata) {
    pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(userdata), 0);
}

void OnStreamReadable(pa_stream* /*stream*/, std::size_t /*nbytes*/, void* userdata) {
    pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(userdata), 0);
}

}  // namespace
#endif

PulseMonitorCapture::PulseMonitorCapture(std::string source_name,
                                         std::shared_ptr<audio::Clock> clock)
    : source_name_(std::move(source_name)), clock_(std::move(clock)) {}

PulseMonitorCapture::~PulseMonitorCapture() {
#ifdef NSPEAKER_HAVE_PULSE
    if (mainloop_ != nullptr) {
        pa_threaded_mainloop_stop(mainloop_);
    }
    if (stream_ != nullptr) {
        pa_stream_disconnect(stream_);
        pa_stream_unref(stream_);
    }
    if (context_ != nullptr) {
        pa_context_disconnect(context_);
        pa_context_unref(context_);
    }
    if (mainloop_ != nullptr) {
        pa_threaded_mainloop_free(mainloop_);
    }
#endif
}

bool PulseMonitorCapture::Start() {
#ifdef NSPEAKER_HAVE_PULSE
    if (source_name_.empty()) {
        source_name_ = ResolveMonitorSourceName();
    }

    // Create threaded mainloop for non-blocking event dispatch.
    mainloop_ = pa_threaded_mainloop_new();
    if (mainloop_ == nullptr) {
        return false;
    }

    context_ = pa_context_new(pa_threaded_mainloop_get_api(mainloop_), "network_speaker");
    if (context_ == nullptr) {
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        return false;
    }

    pa_context_set_state_callback(context_, OnContextStateChanged, mainloop_);

    if (pa_threaded_mainloop_start(mainloop_) < 0) {
        pa_context_unref(context_);
        context_ = nullptr;
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        return false;
    }

    pa_threaded_mainloop_lock(mainloop_);

    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }

    // Wait for the context to reach READY state.
    while (true) {
        const pa_context_state_t state = pa_context_get_state(context_);
        if (state == PA_CONTEXT_READY) {
            break;
        }
        if (!PA_CONTEXT_IS_GOOD(state)) {
            pa_threaded_mainloop_unlock(mainloop_);
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = audio::kDefaultSampleRate;
    spec.channels = audio::kDefaultChannels;

    stream_ = pa_stream_new(context_, "speaker-stream", &spec, nullptr);
    if (stream_ == nullptr) {
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }

    pa_stream_set_state_callback(stream_, OnStreamStateChanged, mainloop_);
    pa_stream_set_read_callback(stream_, OnStreamReadable, mainloop_);

    // Request a fragment size matching one 10 ms frame to minimize buffering.
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<std::uint32_t>(-1);
    attr.fragsize = static_cast<std::uint32_t>(kTargetFloats * sizeof(float));

    const char* device_name = source_name_.empty() ? nullptr : source_name_.c_str();
    const auto flags = static_cast<pa_stream_flags_t>(
        PA_STREAM_ADJUST_LATENCY | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE);

    if (pa_stream_connect_record(stream_, device_name, &attr, flags) < 0) {
        pa_stream_unref(stream_);
        stream_ = nullptr;
        pa_threaded_mainloop_unlock(mainloop_);
        return false;
    }

    // Wait for the stream to become ready.
    while (true) {
        const pa_stream_state_t state = pa_stream_get_state(stream_);
        if (state == PA_STREAM_READY) {
            break;
        }
        if (!PA_STREAM_IS_GOOD(state)) {
            pa_stream_unref(stream_);
            stream_ = nullptr;
            pa_threaded_mainloop_unlock(mainloop_);
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }

    pa_threaded_mainloop_unlock(mainloop_);

    next_tick_ = std::chrono::steady_clock::now();
    started_ = true;
    return true;
#else
    return false;
#endif
}

bool PulseMonitorCapture::ReadFrame(audio::PcmFrame& out) {
#ifdef NSPEAKER_HAVE_PULSE
    if (!started_) {
        return false;
    }

    pa_threaded_mainloop_lock(mainloop_);

    while (pending_.size() < kTargetFloats) {
        if (pa_stream_get_state(stream_) != PA_STREAM_READY) {
            pa_threaded_mainloop_unlock(mainloop_);
            return false;
        }

        const void* data = nullptr;
        std::size_t nbytes = 0;
        if (pa_stream_peek(stream_, &data, &nbytes) < 0) {
            pa_threaded_mainloop_unlock(mainloop_);
            return false;
        }

        if (nbytes == 0) {
            // No data available yet; wait for the read callback to signal.
            pa_threaded_mainloop_wait(mainloop_);
            continue;
        }

        if (data != nullptr) {
            const auto nfloats = nbytes / sizeof(float);
            const auto* floats = static_cast<const float*>(data);
            pending_.insert(pending_.end(), floats, floats + nfloats);
        }
        // data == nullptr with nbytes > 0 indicates a hole; skip it.
        pa_stream_drop(stream_);
    }

    pa_threaded_mainloop_unlock(mainloop_);

    out.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    out.samples_per_channel = audio::kDefaultFrameSamples;
    out.capture_ts_us = clock_->NowMicros();
    out.interleaved.assign(pending_.begin(),
                           pending_.begin() + static_cast<std::ptrdiff_t>(kTargetFloats));
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(kTargetFloats));

    // Maintain a steady 10 ms frame cadence, matching the WASAPI path.
    next_tick_ += kFrameDuration;
    std::this_thread::sleep_until(next_tick_);
    return true;
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
