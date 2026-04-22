#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include "nspeaker/audio/sink.h"

namespace nspeaker::harmony {

// Lock-free single-producer single-consumer ring buffer for float PCM samples.
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity_samples);

    // Write interleaved samples. Returns the number actually written.
    size_t Write(const float* data, size_t count);

    // Read interleaved samples into |dst|. Returns the number actually read.
    size_t Read(float* dst, size_t count);

    size_t Available() const;

private:
    std::vector<float> buf_;
    std::atomic<size_t> head_{0};  // write position
    std::atomic<size_t> tail_{0};  // read position
    size_t capacity_;              // buf_.size() (power-of-two for mask trick)
    size_t mask_;
};

// IAudioSink implementation using HarmonyOS OHAudio native renderer.
// Uses callback (pull) mode with an internal SPSC ring buffer.
class OHAudioSink final : public audio::IAudioSink {
public:
    OHAudioSink();
    ~OHAudioSink() override;

    OHAudioSink(const OHAudioSink&) = delete;
    OHAudioSink& operator=(const OHAudioSink&) = delete;

    bool Open();
    void Start();
    void Stop();

    // IAudioSink interface - called from pipeline worker thread.
    bool SubmitPcm(const audio::PcmFrame& frame) override;

private:
    static int32_t WriteCallback(OH_AudioRenderer* renderer, void* user_data, void* buffer,
                                 int32_t length);

    OH_AudioStreamBuilder* builder_ = nullptr;
    OH_AudioRenderer* renderer_ = nullptr;
    SpscRingBuffer ring_;
    std::atomic<bool> started_{false};
};

}  // namespace nspeaker::harmony
