#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "nspeaker/audio/stream_stats.h"
#include "nspeaker/transport/audio_packet.h"

namespace nspeaker::transport {

class JitterBuffer {
public:
    explicit JitterBuffer(std::size_t target_packets, std::size_t max_window = 64);

    bool Push(AudioPacket packet, std::uint32_t expected_sequence, audio::StreamStats& stats);
    [[nodiscard]] std::optional<AudioPacket> PopNext(std::uint32_t expected_sequence);
    [[nodiscard]] std::optional<std::uint32_t> OldestSequence() const;
    void Reset();
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] bool Primed() const noexcept;
    [[nodiscard]] std::size_t target_packets() const noexcept;

private:
    std::size_t target_packets_;
    std::size_t max_window_;
    std::map<std::uint32_t, AudioPacket> packets_;
};

}  // namespace nspeaker::transport
