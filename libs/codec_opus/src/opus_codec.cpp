#include "nspeaker/codec/opus_codec.h"

#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#elif __has_include(<opus.h>)
#include <opus.h>
#else
#error "Opus headers not found"
#endif

namespace nspeaker::codec {

struct OpusEncoder::Impl {
    OpusEncoder* owner = nullptr;
    ::OpusEncoder* handle = nullptr;
    bool ok = false;
};

struct OpusDecoder::Impl {
    ::OpusDecoder* handle = nullptr;
    bool ok = false;
};

OpusEncoder::OpusEncoder() : impl_(std::make_unique<Impl>()) {
    int error = OPUS_OK;
    impl_->handle = opus_encoder_create(audio::kDefaultSampleRate, audio::kDefaultChannels,
                                        OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
    impl_->ok = (error == OPUS_OK && impl_->handle != nullptr);
    if (impl_->ok) {
        opus_encoder_ctl(impl_->handle, OPUS_SET_BITRATE(192000));
        opus_encoder_ctl(impl_->handle, OPUS_SET_VBR(0));
        opus_encoder_ctl(impl_->handle, OPUS_SET_COMPLEXITY(10));
        opus_encoder_ctl(impl_->handle, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    }
}

OpusEncoder::~OpusEncoder() {
    if (impl_ != nullptr && impl_->handle != nullptr) {
        opus_encoder_destroy(impl_->handle);
    }
}

bool OpusEncoder::Encode(const audio::PcmFrame& pcm, std::vector<std::uint8_t>& opus) {
    if (!ok() || pcm.format.sample_rate != audio::kDefaultSampleRate ||
        pcm.format.channels != audio::kDefaultChannels || pcm.samples_per_channel == 0 ||
        pcm.interleaved.size() <
            static_cast<std::size_t>(pcm.samples_per_channel * audio::kDefaultChannels)) {
        return false;
    }

    opus.resize(1500);
    const auto packet_size = opus_encode_float(impl_->handle, pcm.interleaved.data(),
                                               static_cast<int>(pcm.samples_per_channel),
                                               opus.data(), static_cast<opus_int32>(opus.size()));
    if (packet_size < 0) {
        return false;
    }

    opus.resize(static_cast<std::size_t>(packet_size));
    return true;
}

bool OpusEncoder::ok() const noexcept {
    return impl_ != nullptr && impl_->ok;
}

OpusDecoder::OpusDecoder() : impl_(std::make_unique<Impl>()) {
    int error = OPUS_OK;
    impl_->handle = opus_decoder_create(audio::kDefaultSampleRate, audio::kDefaultChannels, &error);
    impl_->ok = (error == OPUS_OK && impl_->handle != nullptr);
}

OpusDecoder::~OpusDecoder() {
    if (impl_ != nullptr && impl_->handle != nullptr) {
        opus_decoder_destroy(impl_->handle);
    }
}

bool OpusDecoder::Decode(std::span<const std::uint8_t> opus, std::uint16_t frame_samples,
                         audio::PcmFrame& pcm) {
    if (!ok() || opus.empty() || frame_samples == 0) {
        return false;
    }

    pcm.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    pcm.samples_per_channel = frame_samples;
    pcm.interleaved.resize(static_cast<std::size_t>(frame_samples * audio::kDefaultChannels));

    const auto decoded =
        opus_decode_float(impl_->handle, opus.data(), static_cast<opus_int32>(opus.size()),
                          pcm.interleaved.data(), frame_samples, 0);
    return decoded == frame_samples;
}

bool OpusDecoder::DecodePLC(std::uint16_t frame_samples, audio::PcmFrame& pcm) {
    if (!ok() || frame_samples == 0) {
        return false;
    }

    pcm.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    pcm.samples_per_channel = frame_samples;
    pcm.interleaved.resize(static_cast<std::size_t>(frame_samples * audio::kDefaultChannels));

    const auto decoded =
        opus_decode_float(impl_->handle, nullptr, 0, pcm.interleaved.data(), frame_samples, 0);
    return decoded == frame_samples;
}

bool OpusDecoder::DecodeFEC(std::span<const std::uint8_t> next_opus, std::uint16_t frame_samples,
                            audio::PcmFrame& pcm) {
    if (!ok() || next_opus.empty() || frame_samples == 0) {
        return false;
    }

    pcm.format = {.sample_rate = audio::kDefaultSampleRate, .channels = audio::kDefaultChannels};
    pcm.samples_per_channel = frame_samples;
    pcm.interleaved.resize(static_cast<std::size_t>(frame_samples * audio::kDefaultChannels));

    // fec=1: attempt to decode the FEC data embedded in next_opus.
    // For CELT-only streams (RESTRICTED_LOWDELAY) the codec falls back to PLC internally.
    const auto decoded = opus_decode_float(impl_->handle, next_opus.data(),
                                           static_cast<opus_int32>(next_opus.size()),
                                           pcm.interleaved.data(), frame_samples, 1);
    return decoded == frame_samples;
}

bool OpusDecoder::Reset() {
    if (!ok()) {
        return false;
    }
    return opus_decoder_ctl(impl_->handle, OPUS_RESET_STATE) == OPUS_OK;
}

bool OpusDecoder::ok() const noexcept {
    return impl_ != nullptr && impl_->ok;
}

}  // namespace nspeaker::codec
