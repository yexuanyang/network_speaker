#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "nspeaker/audio/frame.h"

namespace nspeaker::codec {

class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;

    virtual bool Encode(const audio::PcmFrame& pcm, std::vector<std::uint8_t>& opus) = 0;
};

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    virtual bool Decode(std::span<const std::uint8_t> opus, std::uint16_t frame_samples,
                        audio::PcmFrame& pcm) = 0;

    /// Generate a concealment frame for a lost packet using decoder internal state.
    virtual bool DecodePLC(std::uint16_t frame_samples, audio::PcmFrame& pcm) = 0;

    /// Decode the FEC data embedded in the *next* packet to reconstruct the lost frame.
    /// Falls back to PLC when no FEC data is present (e.g. CELT-only mode).
    virtual bool DecodeFEC(std::span<const std::uint8_t> next_opus, std::uint16_t frame_samples,
                           audio::PcmFrame& pcm) = 0;

    virtual bool Reset() = 0;
};

class OpusEncoder final : public IAudioEncoder {
public:
    OpusEncoder();
    ~OpusEncoder() override;

    OpusEncoder(const OpusEncoder&) = delete;
    OpusEncoder& operator=(const OpusEncoder&) = delete;

    [[nodiscard]] bool Encode(const audio::PcmFrame& pcm, std::vector<std::uint8_t>& opus) override;
    [[nodiscard]] bool ok() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OpusDecoder final : public IAudioDecoder {
public:
    OpusDecoder();
    ~OpusDecoder() override;

    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    [[nodiscard]] bool Decode(std::span<const std::uint8_t> opus, std::uint16_t frame_samples,
                              audio::PcmFrame& pcm) override;
    [[nodiscard]] bool DecodePLC(std::uint16_t frame_samples, audio::PcmFrame& pcm) override;
    [[nodiscard]] bool DecodeFEC(std::span<const std::uint8_t> next_opus,
                                 std::uint16_t frame_samples, audio::PcmFrame& pcm) override;
    [[nodiscard]] bool Reset() override;
    [[nodiscard]] bool ok() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nspeaker::codec
