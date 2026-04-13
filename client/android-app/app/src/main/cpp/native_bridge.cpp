#include <jni.h>
#include <android/log.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "nspeaker/client/callback_audio_sink.h"
#include "nspeaker/client/client_session.h"

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_networkspeaker_NativeBridge_nativeStart(JNIEnv* env, jclass clazz,
                                                         jstring host, jint port);
extern "C" JNIEXPORT void JNICALL
Java_com_example_networkspeaker_NativeBridge_nativeStop(JNIEnv* env, jclass clazz);

namespace {

constexpr char kLogTag[] = "NetworkSpeakerNative";

JavaVM* g_vm = nullptr;
jclass g_bridge_class = nullptr;
jmethodID g_on_pcm_ready = nullptr;
std::mutex g_mutex;
std::unique_ptr<nspeaker::client::ClientSession> g_session;

bool SubmitToJava(const nspeaker::audio::PcmFrame& frame) {
    if (g_vm == nullptr || g_bridge_class == nullptr || g_on_pcm_ready == nullptr) {
        return false;
    }

    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return false;
        }
        attached = true;
    }

    auto* array = env->NewFloatArray(static_cast<jsize>(frame.interleaved.size()));
    if (array == nullptr) {
        if (attached) {
            g_vm->DetachCurrentThread();
        }
        return false;
    }

    env->SetFloatArrayRegion(array, 0, static_cast<jsize>(frame.interleaved.size()),
                             frame.interleaved.data());
    env->CallStaticVoidMethod(g_bridge_class, g_on_pcm_ready, array,
                              static_cast<jint>(frame.samples_per_channel));
    const bool ok = !env->ExceptionCheck();
    if (!ok) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(array);

    if (attached) {
        g_vm->DetachCurrentThread();
    }
    return ok;
}

}  // namespace

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    static_cast<void>(reserved);
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_networkspeaker_NativeBridge_nativeStart(JNIEnv* env, jclass clazz,
                                                         jstring host, jint port) {
    std::scoped_lock lock(g_mutex);
    const char* raw_host = env->GetStringUTFChars(host, nullptr);
    const std::string allowed_sender_ipv4 = raw_host != nullptr ? raw_host : "";
    if (raw_host != nullptr) {
        env->ReleaseStringUTFChars(host, raw_host);
    }

    if (g_bridge_class == nullptr) {
        g_bridge_class = static_cast<jclass>(env->NewGlobalRef(clazz));
        g_on_pcm_ready = env->GetStaticMethodID(g_bridge_class, "onPcmReady", "([FI)V");
    }

    if (g_session != nullptr) {
        g_session->Stop();
        g_session.reset();
    }

    auto sink = std::make_shared<nspeaker::client::CallbackAudioSink>(SubmitToJava);
    g_session = std::make_unique<nspeaker::client::ClientSession>(
        nspeaker::client::ClientSession::Config{
            .listen_port = static_cast<std::uint16_t>(port),
            .allowed_sender_ipv4 = allowed_sender_ipv4,
            .pipeline_config = {},  // use PipelineConfig defaults (FastLock enabled, target=3)
            .poll_timeout = std::chrono::milliseconds(20),
        },
        sink);

    if (!g_session->Start()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Failed to start ClientSession on UDP port %d", port);
        g_session.reset();
        return JNI_FALSE;
    } else {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ClientSession started on UDP port %d hostFilter=%s", port,
                            allowed_sender_ipv4.empty() ? "<any>" : allowed_sender_ipv4.c_str());
        return JNI_TRUE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_networkspeaker_NativeBridge_nativeStop(JNIEnv* env, jclass clazz) {
    static_cast<void>(env);
    static_cast<void>(clazz);
    std::scoped_lock lock(g_mutex);
    if (g_session != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Stopping ClientSession");
        g_session->Stop();
        g_session.reset();
    }
}
