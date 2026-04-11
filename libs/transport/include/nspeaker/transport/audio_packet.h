#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "nspeaker/audio/frame.h"

namespace nspeaker::transport {

inline constexpr std::uint32_t kPacketMagic = 0x4E535031;
inline constexpr std::uint16_t kPacketVersion = 1;
inline constexpr std::size_t kAudioPacketHeaderSize = 28;

struct AudioPacketHeader {
    std::uint32_t magic = kPacketMagic;
    std::uint16_t version = kPacketVersion;
    std::uint16_t flags = 0;
    std::uint32_t stream_id = 1;
    std::uint32_t sequence = 0;
    std::uint64_t capture_ts_us = 0;
    std::uint16_t frame_samples = audio::kDefaultFrameSamples;
    std::uint16_t payload_size = 0;
};

struct AudioPacket {
    AudioPacketHeader header{};
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> SerializePacket(const AudioPacket& packet);
[[nodiscard]] std::optional<AudioPacket> TryParsePacket(std::span<const std::uint8_t> buffer);

}  // namespace nspeaker::transport
