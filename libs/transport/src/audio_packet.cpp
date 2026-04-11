#include "nspeaker/transport/audio_packet.h"

#include <array>

namespace nspeaker::transport {
namespace {

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint16_t ReadU16(std::span<const std::uint8_t> buffer, std::size_t offset) {
    return static_cast<std::uint16_t>((buffer[offset] << 8) | buffer[offset + 1]);
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> buffer, std::size_t offset) {
    return (static_cast<std::uint32_t>(buffer[offset]) << 24) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buffer[offset + 3]);
}

[[nodiscard]] std::uint64_t ReadU64(std::span<const std::uint8_t> buffer, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | buffer[offset + i];
    }
    return value;
}

}  // namespace

std::vector<std::uint8_t> SerializePacket(const AudioPacket& packet) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kAudioPacketHeaderSize + packet.payload.size());

    WriteU32(bytes, packet.header.magic);
    WriteU16(bytes, packet.header.version);
    WriteU16(bytes, packet.header.flags);
    WriteU32(bytes, packet.header.stream_id);
    WriteU32(bytes, packet.header.sequence);
    WriteU64(bytes, packet.header.capture_ts_us);
    WriteU16(bytes, packet.header.frame_samples);
    WriteU16(bytes, static_cast<std::uint16_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}

std::optional<AudioPacket> TryParsePacket(std::span<const std::uint8_t> buffer) {
    if (buffer.size() < kAudioPacketHeaderSize) {
        return std::nullopt;
    }

    AudioPacket packet;
    packet.header.magic = ReadU32(buffer, 0);
    packet.header.version = ReadU16(buffer, 4);
    packet.header.flags = ReadU16(buffer, 6);
    packet.header.stream_id = ReadU32(buffer, 8);
    packet.header.sequence = ReadU32(buffer, 12);
    packet.header.capture_ts_us = ReadU64(buffer, 16);
    packet.header.frame_samples = ReadU16(buffer, 24);
    packet.header.payload_size = ReadU16(buffer, 26);

    if (packet.header.magic != kPacketMagic || packet.header.version != kPacketVersion) {
        return std::nullopt;
    }
    if (buffer.size() != kAudioPacketHeaderSize + packet.header.payload_size) {
        return std::nullopt;
    }

    packet.payload.assign(buffer.begin() + static_cast<std::ptrdiff_t>(kAudioPacketHeaderSize),
                          buffer.end());
    return packet;
}

}  // namespace nspeaker::transport
