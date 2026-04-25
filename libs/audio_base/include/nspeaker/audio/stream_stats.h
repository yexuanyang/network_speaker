#pragma once

#include <cstdint>

namespace nspeaker::audio {

struct StreamStats {
    std::uint32_t packets_sent = 0;
    std::uint32_t packets_received = 0;
    std::uint32_t packets_lost = 0;
    std::uint32_t packets_reordered = 0;
    std::uint32_t decode_failures = 0;
    std::uint32_t playback_underruns = 0;
    std::uint32_t duplicates_dropped = 0;
    std::uint32_t late_dropped = 0;
    std::uint32_t current_jitter_ms = 0;
    std::uint32_t peak_jitter_ms = 0;
    std::uint32_t e2e_latency_ms = 0;
    std::uint32_t startup_skipped_packets = 0;
    std::uint32_t jitter_variance_us = 0;
    std::uint32_t plc_concealed = 0;  // frames generated via PLC/FEC concealment
    std::uint32_t fec_recovered = 0;  // frames recovered using FEC from the next packet
};

}  // namespace nspeaker::audio
