#include "ohaudio_sink.h"

#include <algorithm>
#include <cstring>

#include <hilog/log.h>

namespace nspeaker::harmony {

namespace {
constexpr unsigned int kLogDomain = 0x0000;
constexpr char kLogTag[] = "OHAudioSink";

// Ring buffer capacity: 10 frames * 480 samples * 2 channels = 9600 floats.
// Round up to next power of two = 16384.
constexpr size_t kRingCapacitySamples = 16384;

size_t NextPowerOfTwo(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}
}  // namespace

// --- SpscRingBuffer ---

SpscRingBuffer::SpscRingBuffer(size_t capacity_samples) {
    capacity_ = NextPowerOfTwo(capacity_samples < 2 ? 2 : capacity_samples);
    mask_ = capacity_ - 1;
    buf_.resize(capacity_, 0.0f);
}

size_t SpscRingBuffer::Write(const float* data, size_t count) {
    const size_t h = head_.load(std::memory_order_relaxed);
    const size_t t = tail_.load(std::memory_order_acquire);
    const size_t free = capacity_ - (h - t);
    const size_t n = std::min(count, free);
    for (size_t i = 0; i < n; ++i) {
        buf_[(h + i) & mask_] = data[i];
    }
    head_.store(h + n, std::memory_order_release);
    return n;
}

size_t SpscRingBuffer::Read(float* dst, size_t count) {
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t h = head_.load(std::memory_order_acquire);
    const size_t avail = h - t;
    const size_t n = std::min(count, avail);
    for (size_t i = 0; i < n; ++i) {
        dst[i] = buf_[(t + i) & mask_];
    }
    tail_.store(t + n, std::memory_order_release);
    return n;
}

size_t SpscRingBuffer::Available() const {
    const size_t h = head_.load(std::memory_order_acquire);
    const size_t t = tail_.load(std::memory_order_acquire);
    return h - t;
}

// --- OHAudioSink ---

OHAudioSink::OHAudioSink() : ring_(kRingCapacitySamples) {}

OHAudioSink::~OHAudioSink() {
    Stop();
    if (renderer_) {
        OH_AudioRenderer_Release(renderer_);
        renderer_ = nullptr;
    }
    if (builder_) {
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
    }
}

bool OHAudioSink::Open() {
    OH_AudioStream_Result result =
        OH_AudioStreamBuilder_Create(&builder_, AUDIOSTREAM_TYPE_RENDERER);
    if (result != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "OHAudioSink: Failed to create stream builder: %{public}d", result);
        return false;
    }

    OH_AudioStreamBuilder_SetSamplingRate(builder_, 48000);
    OH_AudioStreamBuilder_SetChannelCount(builder_, 2);
    OH_AudioStreamBuilder_SetSampleFormat(builder_, AUDIOSTREAM_SAMPLE_F32LE);
    OH_AudioStreamBuilder_SetEncodingType(builder_, AUDIOSTREAM_ENCODING_TYPE_RAW);
    OH_AudioStreamBuilder_SetRendererInfo(builder_, AUDIOSTREAM_USAGE_MUSIC);
    OH_AudioStreamBuilder_SetLatencyMode(builder_, AUDIOSTREAM_LATENCY_MODE_FAST);

    OH_AudioRenderer_Callbacks callbacks{};
    callbacks.OH_AudioRenderer_OnWriteData = WriteCallback;
    OH_AudioStreamBuilder_SetRendererCallback(builder_, callbacks, this);

    result = OH_AudioStreamBuilder_GenerateRenderer(builder_, &renderer_);
    if (result != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "OHAudioSink: Failed to generate renderer: %{public}d", result);
        return false;
    }

    OH_LOG_INFO(LOG_APP, "OHAudioSink: Opened (48kHz, stereo, float32, low-latency)");
    return true;
}

void OHAudioSink::Start() {
    if (renderer_ && !started_.load(std::memory_order_relaxed)) {
        OH_AudioStream_Result result = OH_AudioRenderer_Start(renderer_);
        if (result == AUDIOSTREAM_SUCCESS) {
            started_.store(true, std::memory_order_release);
            OH_LOG_INFO(LOG_APP, "OHAudioSink: Started");
        } else {
            OH_LOG_ERROR(LOG_APP, "OHAudioSink: Failed to start renderer: %{public}d", result);
        }
    }
}

void OHAudioSink::Stop() {
    if (renderer_ && started_.load(std::memory_order_relaxed)) {
        OH_AudioRenderer_Stop(renderer_);
        started_.store(false, std::memory_order_release);
        OH_LOG_INFO(LOG_APP, "OHAudioSink: Stopped");
    }
}

bool OHAudioSink::SubmitPcm(const audio::PcmFrame& frame) {
    if (!started_.load(std::memory_order_acquire)) {
        return false;
    }
    const size_t total = frame.interleaved.size();
    const size_t written = ring_.Write(frame.interleaved.data(), total);

    static std::atomic<int> submit_count{0};
    const int count = ++submit_count;
    if (count == 1 || count % 100 == 0) {
        OH_LOG_INFO(LOG_APP, "OHAudioSink: submitted %{public}d frames (total=%{public}d)",
                    static_cast<int>(frame.samples_per_channel), count);
    }

    return written == total;
}

int32_t OHAudioSink::WriteCallback(OH_AudioRenderer* renderer, void* user_data, void* buffer,
                                   int32_t length) {
    auto* self = static_cast<OHAudioSink*>(user_data);
    auto* dst = static_cast<float*>(buffer);
    const size_t sample_count = static_cast<size_t>(length) / sizeof(float);
    const size_t read = self->ring_.Read(dst, sample_count);
    // Zero-fill any remaining buffer (underrun protection).
    if (read < sample_count) {
        std::memset(dst + read, 0, (sample_count - read) * sizeof(float));
    }
    return 0;
}

}  // namespace nspeaker::harmony
