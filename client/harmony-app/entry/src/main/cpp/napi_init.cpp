#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <hilog/log.h>
#include <napi/native_api.h>

#include "nspeaker/client/client_session.h"
#include "ohaudio_sink.h"

namespace {

constexpr char kLogTag[] = "NetworkSpeakerNapi";

std::mutex g_mutex;
std::unique_ptr<nspeaker::client::ClientSession> g_session;
std::shared_ptr<nspeaker::harmony::OHAudioSink> g_audio_sink;
// 0 = idle, 1 = running, 2 = failed
std::atomic<int> g_state{0};

// --- NAPI helpers ---

std::string NapiGetString(napi_env env, napi_value value) {
    size_t len = 0;
    napi_get_value_string_utf8(env, value, nullptr, 0, &len);
    std::string result(len, '\0');
    napi_get_value_string_utf8(env, value, result.data(), len + 1, &len);
    return result;
}

// --- Exported functions ---

napi_value NativeStart(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    std::string host = NapiGetString(env, args[0]);
    int32_t port = 0;
    napi_get_value_int32(env, args[1], &port);

    std::scoped_lock lock(g_mutex);

    // Stop any existing session.
    if (g_session) {
        g_session->Stop();
        g_session.reset();
    }
    if (g_audio_sink) {
        g_audio_sink->Stop();
        g_audio_sink.reset();
    }

    // Create OHAudio sink.
    g_audio_sink = std::make_shared<nspeaker::harmony::OHAudioSink>();
    if (!g_audio_sink->Open()) {
        OH_LOG_ERROR(LOG_APP, "NativeStart: Failed to open OHAudioSink");
        g_audio_sink.reset();
        g_state.store(2, std::memory_order_release);

        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    // Create and start client session.
    g_session = std::make_unique<nspeaker::client::ClientSession>(
        nspeaker::client::ClientSession::Config{
            .listen_port = static_cast<std::uint16_t>(port),
            .allowed_sender_ipv4 = host,
            .pipeline_config =
                nspeaker::client::PipelineConfig{
                    .startup_fast_lock_enabled = false,
                    .steady_target_packets = 6,
                    .stale_packet_threshold_ms = 0,
                    .late_frame_drop_threshold_ms = 0,
                },
            .poll_timeout = std::chrono::milliseconds(20),
        },
        g_audio_sink);

    bool ok = g_session->Start();
    if (ok) {
        g_audio_sink->Start();
        g_state.store(1, std::memory_order_release);
        OH_LOG_INFO(LOG_APP,
                    "NativeStart: Session started on port %{public}d hostFilter=%{public}s", port,
                    host.empty() ? "<any>" : host.c_str());
    } else {
        OH_LOG_ERROR(LOG_APP, "NativeStart: Failed to start session on port %{public}d", port);
        g_session.reset();
        g_audio_sink.reset();
        g_state.store(2, std::memory_order_release);
    }

    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

napi_value NativeStop(napi_env env, napi_callback_info info) {
    std::scoped_lock lock(g_mutex);
    if (g_session) {
        OH_LOG_INFO(LOG_APP, "NativeStop: Stopping session");
        g_session->Stop();
        g_session.reset();
    }
    if (g_audio_sink) {
        g_audio_sink->Stop();
        g_audio_sink.reset();
    }
    g_state.store(0, std::memory_order_release);

    return nullptr;
}

napi_value GetReceiverState(napi_env env, napi_callback_info info) {
    const int state = g_state.load(std::memory_order_acquire);
    const char* text = "idle";
    if (state == 1) {
        text = "running";
    } else if (state == 2) {
        text = "failed";
    }

    napi_value result;
    napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &result);
    return result;
}

}  // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"nativeStart", nullptr, NativeStart, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"nativeStop", nullptr, NativeStop, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getReceiverState", nullptr, GetReceiverState, nullptr, nullptr, nullptr, napi_default,
         nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module entryModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) {
    napi_module_register(&entryModule);
}
