#include "nspeaker/server/platform/windows/wasapi_device_enumerator.h"

#ifdef NSPEAKER_HAVE_PULSE

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>

namespace nspeaker::server {
namespace {

struct DeviceLookup {
    std::string default_sink_name;
    std::vector<AudioDeviceInfo> devices;
    bool server_info_done = false;
    bool sink_list_done = false;
};

void OnSinkListInfo(pa_context* /*context*/, const pa_sink_info* info, int eol, void* userdata) {
    auto* lookup = static_cast<DeviceLookup*>(userdata);
    if (eol != 0) {
        lookup->sink_list_done = true;
        return;
    }
    if (info == nullptr) {
        return;
    }

    AudioDeviceInfo device;
    device.id = (info->monitor_source_name != nullptr) ? info->monitor_source_name : "";
    device.name = (info->description != nullptr) ? info->description : "";
    device.description = (info->name != nullptr) ? info->name : "";
    device.is_default = (info->name != nullptr && lookup->default_sink_name == info->name);
    lookup->devices.push_back(std::move(device));
}

void OnServerInfo(pa_context* context, const pa_server_info* info, void* userdata) {
    auto* lookup = static_cast<DeviceLookup*>(userdata);
    if (info != nullptr && info->default_sink_name != nullptr) {
        lookup->default_sink_name = info->default_sink_name;
    }
    lookup->server_info_done = true;

    if (pa_operation* op = pa_context_get_sink_info_list(context, OnSinkListInfo, lookup);
        op != nullptr) {
        pa_operation_unref(op);
    } else {
        lookup->sink_list_done = true;
    }
}

}  // namespace

std::vector<AudioDeviceInfo> EnumerateAudioRenderDevices() {
    pa_mainloop* mainloop = pa_mainloop_new();
    if (mainloop == nullptr) {
        return {};
    }

    pa_context* context = pa_context_new(pa_mainloop_get_api(mainloop), "network_speaker_enum");
    if (context == nullptr) {
        pa_mainloop_free(mainloop);
        return {};
    }

    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return {};
    }

    DeviceLookup lookup;
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
            if (lookup.server_info_done && lookup.sink_list_done) {
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
    return lookup.devices;
}

}  // namespace nspeaker::server

#endif  // NSPEAKER_HAVE_PULSE
