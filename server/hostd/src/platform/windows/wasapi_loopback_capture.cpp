#include "nspeaker/server/platform/windows/wasapi_loopback_capture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <windows.h>
#endif

namespace nspeaker::server {

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kTargetChannels = audio::kDefaultChannels;
constexpr std::uint32_t kTargetSamplesPerFrame = audio::kDefaultFrameSamples;
constexpr std::uint32_t kTargetFrameFloats = kTargetChannels * kTargetSamplesPerFrame;
constexpr double kCenterMixGain = 0.7071067811865476;
constexpr double kLfeMixGain = 0.5;
constexpr auto kFrameDuration = std::chrono::milliseconds(10);

struct CoTaskMemDeleter {
    void operator()(WAVEFORMATEX* format) const noexcept {
        if (format != nullptr) {
            CoTaskMemFree(format);
        }
    }
};

using WaveFormatPtr = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

enum class SampleEncoding {
    kUnknown,
    kFloat32,
    kPcm
};

bool IsExtensibleFormat(const WAVEFORMATEX& format) {
    return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
}

std::uint32_t ChannelBitForIndex(std::uint32_t channel_mask, int channel_index) {
    if (channel_mask == 0 || channel_index < 0) {
        return 0;
    }

    int current_index = 0;
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        const std::uint32_t value = (1u << bit);
        if ((channel_mask & value) == 0) {
            continue;
        }
        if (current_index == channel_index) {
            return value;
        }
        ++current_index;
    }

    return 0;
}

float ClampUnit(double sample) {
    return static_cast<float>(std::clamp(sample, -1.0, 1.0));
}

float DecodePcmSample(const std::byte* sample_bytes, int bits_per_sample, int valid_bits_per_sample) {
    if (bits_per_sample <= 0) {
        return 0.0F;
    }

    const int effective_bits =
        (valid_bits_per_sample > 0 && valid_bits_per_sample <= bits_per_sample) ? valid_bits_per_sample
                                                                                : bits_per_sample;

    if (bits_per_sample == 8) {
        const auto raw = static_cast<int>(std::to_integer<unsigned char>(sample_bytes[0]));
        return static_cast<float>((raw - 128) / 128.0);
    }

    if (bits_per_sample == 16) {
        std::int16_t value = 0;
        std::memcpy(&value, sample_bytes, sizeof(value));
        if (effective_bits < bits_per_sample) {
            value = static_cast<std::int16_t>(value >> (bits_per_sample - effective_bits));
        }
        const auto scale = static_cast<double>(1ULL << (effective_bits - 1));
        return ClampUnit(static_cast<double>(value) / scale);
    }

    if (bits_per_sample == 24) {
        std::int32_t value = static_cast<std::int32_t>(std::to_integer<unsigned char>(sample_bytes[0])) |
                             (static_cast<std::int32_t>(std::to_integer<unsigned char>(sample_bytes[1])) << 8) |
                             (static_cast<std::int32_t>(std::to_integer<unsigned char>(sample_bytes[2])) << 16);
        if ((value & 0x00800000) != 0) {
            value |= ~0x00FFFFFF;
        }
        if (effective_bits < bits_per_sample) {
            value >>= (bits_per_sample - effective_bits);
        }
        const auto scale = static_cast<double>(1ULL << (effective_bits - 1));
        return ClampUnit(static_cast<double>(value) / scale);
    }

    if (bits_per_sample == 32) {
        std::int32_t value = 0;
        std::memcpy(&value, sample_bytes, sizeof(value));
        if (effective_bits < bits_per_sample) {
            value >>= (bits_per_sample - effective_bits);
        }
        const auto scale = static_cast<double>(1ULL << (effective_bits - 1));
        return ClampUnit(static_cast<double>(value) / scale);
    }

    return 0.0F;
}

}  // namespace
#endif

struct WasapiLoopbackCapture::Impl {
    explicit Impl(std::shared_ptr<audio::Clock> capture_clock) : clock(std::move(capture_clock)) {}

    std::shared_ptr<audio::Clock> clock;

#ifdef _WIN32
    std::chrono::steady_clock::time_point next_tick{};
    WaveFormatPtr mix_format;
    ComPtr<IAudioClient> audio_client;
    ComPtr<IAudioCaptureClient> capture_client;
    std::vector<float> source_stereo;
    std::vector<float> pending_output;
    std::uint32_t input_sample_rate = 0;
    std::uint16_t input_channels = 0;
    std::uint16_t input_bits_per_sample = 0;
    std::uint16_t input_valid_bits_per_sample = 0;
    std::uint32_t input_channel_mask = 0;
    REFERENCE_TIME device_period = 0;
    double source_position = 0.0;
    SampleEncoding sample_encoding = SampleEncoding::kUnknown;
    bool started = false;
    bool should_uninitialize_com = false;

    void Shutdown() noexcept {
        if (audio_client != nullptr) {
            audio_client->Stop();
        }
        capture_client.Reset();
        audio_client.Reset();
        mix_format.reset();
        source_stereo.clear();
        pending_output.clear();
        source_position = 0.0;
        input_sample_rate = 0;
        input_channels = 0;
        input_bits_per_sample = 0;
        input_valid_bits_per_sample = 0;
        input_channel_mask = 0;
        sample_encoding = SampleEncoding::kUnknown;
        started = false;
        if (should_uninitialize_com) {
            CoUninitialize();
            should_uninitialize_com = false;
        }
    }

    bool ConfigureMixFormat() {
        if (mix_format == nullptr) {
            return false;
        }

        input_sample_rate = mix_format->nSamplesPerSec;
        input_channels = mix_format->nChannels;
        input_bits_per_sample = mix_format->wBitsPerSample;
        input_valid_bits_per_sample = mix_format->wBitsPerSample;
        input_channel_mask = 0;
        sample_encoding = SampleEncoding::kUnknown;

        if (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            sample_encoding = SampleEncoding::kFloat32;
        } else if (mix_format->wFormatTag == WAVE_FORMAT_PCM) {
            sample_encoding = SampleEncoding::kPcm;
        } else if (IsExtensibleFormat(*mix_format)) {
            const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format.get());
            input_valid_bits_per_sample = extensible->Samples.wValidBitsPerSample == 0
                                              ? extensible->Format.wBitsPerSample
                                              : extensible->Samples.wValidBitsPerSample;
            input_channel_mask = extensible->dwChannelMask;
            if (extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                sample_encoding = SampleEncoding::kFloat32;
            } else if (extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
                sample_encoding = SampleEncoding::kPcm;
            }
        }

        return sample_encoding != SampleEncoding::kUnknown && input_sample_rate > 0 && input_channels > 0 &&
               input_bits_per_sample > 0;
    }

    float ReadInputSample(const std::byte* sample_bytes) const {
        if (sample_encoding == SampleEncoding::kFloat32 && input_bits_per_sample == 32) {
            float value = 0.0F;
            std::memcpy(&value, sample_bytes, sizeof(value));
            return ClampUnit(value);
        }

        if (sample_encoding == SampleEncoding::kPcm) {
            return DecodePcmSample(sample_bytes, input_bits_per_sample, input_valid_bits_per_sample);
        }

        return 0.0F;
    }

    std::array<float, 2> MixFrameToStereo(const std::byte* frame_bytes) const {
        if (input_channels == 0) {
            return {0.0F, 0.0F};
        }

        const auto bytes_per_sample = static_cast<std::size_t>((input_bits_per_sample + 7) / 8);
        if (bytes_per_sample == 0) {
            return {0.0F, 0.0F};
        }

        if (input_channels == 1) {
            const auto mono = ReadInputSample(frame_bytes);
            return {mono, mono};
        }

        if (input_channel_mask == 0) {
            const auto left = ReadInputSample(frame_bytes);
            const auto right = ReadInputSample(frame_bytes + bytes_per_sample);
            return {left, right};
        }

        double left = 0.0;
        double right = 0.0;
        for (std::uint16_t channel = 0; channel < input_channels; ++channel) {
            const float sample = ReadInputSample(frame_bytes + static_cast<std::size_t>(channel) * bytes_per_sample);
            switch (ChannelBitForIndex(input_channel_mask, channel)) {
            case SPEAKER_FRONT_LEFT:
            case SPEAKER_TOP_FRONT_LEFT:
            case SPEAKER_TOP_BACK_LEFT:
                left += sample;
                break;
            case SPEAKER_FRONT_RIGHT:
            case SPEAKER_TOP_FRONT_RIGHT:
            case SPEAKER_TOP_BACK_RIGHT:
                right += sample;
                break;
            case SPEAKER_FRONT_CENTER:
            case SPEAKER_TOP_FRONT_CENTER:
                left += sample * kCenterMixGain;
                right += sample * kCenterMixGain;
                break;
            case SPEAKER_LOW_FREQUENCY:
                left += sample * kLfeMixGain;
                right += sample * kLfeMixGain;
                break;
            case SPEAKER_BACK_LEFT:
            case SPEAKER_SIDE_LEFT:
                left += sample * kCenterMixGain;
                break;
            case SPEAKER_BACK_RIGHT:
            case SPEAKER_SIDE_RIGHT:
                right += sample * kCenterMixGain;
                break;
            case SPEAKER_BACK_CENTER:
                left += sample * kCenterMixGain;
                right += sample * kCenterMixGain;
                break;
            default:
                if (channel == 0) {
                    left += sample;
                } else if (channel == 1) {
                    right += sample;
                } else {
                    left += sample * kLfeMixGain;
                    right += sample * kLfeMixGain;
                }
                break;
            }
        }

        return {ClampUnit(left), ClampUnit(right)};
    }

    void AppendSilenceFrames(std::uint32_t frame_count) {
        source_stereo.resize(source_stereo.size() + static_cast<std::size_t>(frame_count) * kTargetChannels, 0.0F);
    }

    void AppendConvertedFrames(const std::byte* data, std::uint32_t frame_count, bool silent) {
        if (frame_count == 0) {
            return;
        }

        if (silent || data == nullptr) {
            AppendSilenceFrames(frame_count);
            return;
        }

        const auto bytes_per_sample = static_cast<std::size_t>((input_bits_per_sample + 7) / 8);
        const auto bytes_per_frame = bytes_per_sample * input_channels;
        const auto* current = data;
        source_stereo.reserve(source_stereo.size() + static_cast<std::size_t>(frame_count) * kTargetChannels);
        for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
            const auto stereo = MixFrameToStereo(current);
            source_stereo.push_back(stereo[0]);
            source_stereo.push_back(stereo[1]);
            current += bytes_per_frame;
        }
    }

    void GeneratePendingOutput() {
        if (input_sample_rate == 0 || source_stereo.empty()) {
            return;
        }

        const auto available_frames = source_stereo.size() / kTargetChannels;
        const double ratio = static_cast<double>(input_sample_rate) / audio::kDefaultSampleRate;
        while (source_position < static_cast<double>(available_frames)) {
            const auto left_index = static_cast<std::size_t>(source_position);
            const auto right_index = std::min(left_index + 1, available_frames - 1);
            const double alpha = source_position - static_cast<double>(left_index);

            const auto left0 = source_stereo[left_index * kTargetChannels];
            const auto right0 = source_stereo[left_index * kTargetChannels + 1];
            const auto left1 = source_stereo[right_index * kTargetChannels];
            const auto right1 = source_stereo[right_index * kTargetChannels + 1];

            pending_output.push_back(static_cast<float>(left0 + (left1 - left0) * alpha));
            pending_output.push_back(static_cast<float>(right0 + (right1 - right0) * alpha));

            source_position += ratio;
        }

        const auto drop_frames = static_cast<std::size_t>(source_position);
        if (drop_frames == 0) {
            return;
        }

        const auto safe_drop_frames = std::min(drop_frames, available_frames);
        source_stereo.erase(source_stereo.begin(),
                            source_stereo.begin() + static_cast<std::ptrdiff_t>(safe_drop_frames * kTargetChannels));
        source_position -= static_cast<double>(safe_drop_frames);
    }

    bool PumpCaptureClient() {
        if (capture_client == nullptr) {
            return false;
        }

        while (true) {
            UINT32 packet_frames = 0;
            if (FAILED(capture_client->GetNextPacketSize(&packet_frames))) {
                return false;
            }
            if (packet_frames == 0) {
                break;
            }

            BYTE* data = nullptr;
            UINT32 captured_frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_client->GetBuffer(&data, &captured_frames, &flags, nullptr, nullptr))) {
                return false;
            }

            AppendConvertedFrames(reinterpret_cast<const std::byte*>(data), captured_frames,
                                  (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
            if (FAILED(capture_client->ReleaseBuffer(captured_frames))) {
                return false;
            }
        }

        GeneratePendingOutput();
        return true;
    }
#endif
};

WasapiLoopbackCapture::WasapiLoopbackCapture(std::shared_ptr<audio::Clock> clock)
    : impl_(std::make_unique<Impl>(std::move(clock))) {}

WasapiLoopbackCapture::~WasapiLoopbackCapture() {
#ifdef _WIN32
    impl_->Shutdown();
#endif
}

bool WasapiLoopbackCapture::Start() {
#ifdef _WIN32
    impl_->Shutdown();

    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init_result) && init_result != RPC_E_CHANGED_MODE) {
        return false;
    }
    impl_->should_uninitialize_com = init_result == S_OK || init_result == S_FALSE;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.GetAddressOf())))) {
        impl_->Shutdown();
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf()))) {
        impl_->Shutdown();
        return false;
    }

    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(impl_->audio_client.GetAddressOf())))) {
        impl_->Shutdown();
        return false;
    }

    WAVEFORMATEX* raw_mix_format = nullptr;
    if (FAILED(impl_->audio_client->GetMixFormat(&raw_mix_format))) {
        impl_->Shutdown();
        return false;
    }
    impl_->mix_format.reset(raw_mix_format);

    if (!impl_->ConfigureMixFormat()) {
        impl_->Shutdown();
        return false;
    }

    impl_->audio_client->GetDevicePeriod(&impl_->device_period, nullptr);
    if (FAILED(impl_->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0,
                                               impl_->mix_format.get(), nullptr))) {
        impl_->Shutdown();
        return false;
    }

    if (FAILED(impl_->audio_client->GetService(IID_PPV_ARGS(impl_->capture_client.GetAddressOf())))) {
        impl_->Shutdown();
        return false;
    }

    if (FAILED(impl_->audio_client->Start())) {
        impl_->Shutdown();
        return false;
    }

    impl_->next_tick = std::chrono::steady_clock::now();
    impl_->started = true;
    return true;
#else
    return false;
#endif
}

bool WasapiLoopbackCapture::ReadFrame(audio::PcmFrame& out) {
#ifdef _WIN32
    if (!impl_->started) {
        return false;
    }

    const auto wait_ms = std::max<DWORD>(1, static_cast<DWORD>(impl_->device_period / 20000));
    const auto target_deadline = impl_->next_tick + kFrameDuration;
    while (impl_->pending_output.size() < kTargetFrameFloats) {
        if (!impl_->PumpCaptureClient()) {
            return false;
        }
        if (impl_->pending_output.size() >= kTargetFrameFloats) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= target_deadline) {
            break;
        }
        Sleep(wait_ms);
    }

    if (impl_->pending_output.size() < kTargetFrameFloats) {
        impl_->pending_output.resize(kTargetFrameFloats, 0.0F);
    }

    out.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    out.samples_per_channel = audio::kDefaultFrameSamples;
    out.capture_ts_us = impl_->clock->NowMicros();
    out.interleaved.assign(impl_->pending_output.begin(), impl_->pending_output.begin() + kTargetFrameFloats);
    impl_->pending_output.erase(impl_->pending_output.begin(),
                                impl_->pending_output.begin() + static_cast<std::ptrdiff_t>(kTargetFrameFloats));

    impl_->next_tick += kFrameDuration;
    std::this_thread::sleep_until(impl_->next_tick);
    return true;
#else
    static_cast<void>(out);
    return false;
#endif
}

}  // namespace nspeaker::server
