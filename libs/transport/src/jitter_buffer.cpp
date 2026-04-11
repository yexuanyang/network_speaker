#include "nspeaker/transport/jitter_buffer.h"

namespace nspeaker::transport {

JitterBuffer::JitterBuffer(std::size_t target_packets, std::size_t max_window)
    : target_packets_(target_packets), max_window_(max_window) {}

bool JitterBuffer::Push(AudioPacket packet, std::uint32_t expected_sequence,
                        audio::StreamStats& stats) {
    if (packet.header.sequence < expected_sequence) {
        ++stats.late_dropped;
        return false;
    }

    if (packet.header.sequence > expected_sequence + max_window_) {
        ++stats.late_dropped;
        return false;
    }

    const auto [it, inserted] = packets_.emplace(packet.header.sequence, std::move(packet));
    if (!inserted) {
        ++stats.duplicates_dropped;
        return false;
    }

    if (it->first > expected_sequence) {
        ++stats.packets_reordered;
    }
    return true;
}

std::optional<AudioPacket> JitterBuffer::PopNext(std::uint32_t expected_sequence) {
    auto it = packets_.find(expected_sequence);
    if (it == packets_.end()) {
        return std::nullopt;
    }

    auto packet = std::move(it->second);
    packets_.erase(it);
    return packet;
}

std::size_t JitterBuffer::Size() const noexcept {
    return packets_.size();
}

bool JitterBuffer::Primed() const noexcept {
    return packets_.size() >= target_packets_;
}

std::size_t JitterBuffer::target_packets() const noexcept {
    return target_packets_;
}

}  // namespace nspeaker::transport
