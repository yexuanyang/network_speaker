#include <cstdlib>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>

#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/server/capture_factory.h"
#include "nspeaker/server/udp_audio_sender.h"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void OnSignal(int /*signal*/) {
    g_stop_requested = 1;
}

void PrintUsage() {
    std::cerr << "Usage: hostd --host <ip> --port <port> [--source sine|pulse|wasapi]"
              << " [--pulse-source <name>] [--seconds <n>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 50000;
    nspeaker::server::CaptureConfig capture_config{};
    std::optional<int> seconds;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--source" && i + 1 < argc) {
            capture_config.source = argv[++i];
        } else if (arg == "--pulse-source" && i + 1 < argc) {
            capture_config.pulse_source_name = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stoi(argv[++i]);
            if (*seconds <= 0) {
                std::cerr << "--seconds must be a positive integer\n";
                return EXIT_FAILURE;
            }
        } else {
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    std::signal(SIGINT, OnSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, OnSignal);
#endif

    auto capture = nspeaker::server::CreateCapture(capture_config);
    if (capture == nullptr) {
        std::cerr << "Unsupported capture source: " << capture_config.source << '\n';
        return EXIT_FAILURE;
    }

    if (!capture->Start()) {
        std::cerr << "Failed to start capture source: " << capture_config.source << '\n';
        return EXIT_FAILURE;
    }

    nspeaker::codec::OpusEncoder encoder;
    if (!encoder.ok()) {
        std::cerr << "Failed to initialize Opus encoder\n";
        return EXIT_FAILURE;
    }

    nspeaker::server::UdpAudioSender sender(host, port);
    if (!sender.Open()) {
        std::cerr << "Failed to open UDP sender\n";
        return EXIT_FAILURE;
    }

    const auto total_frames =
        seconds.has_value() ? std::optional<int>(*seconds * 100) : std::nullopt;
    std::cout << "Streaming to " << host << ':' << port;
    if (seconds.has_value()) {
        std::cout << " for " << *seconds << " seconds";
    } else {
        std::cout << " until interrupted";
    }
    std::cout << '\n';

    std::uint32_t sequence = 0;
    while (!g_stop_requested &&
           (!total_frames.has_value() || sequence < static_cast<std::uint32_t>(*total_frames))) {
        nspeaker::audio::PcmFrame frame;
        if (!capture->ReadFrame(frame)) {
            std::cerr << "Capture read failed\n";
            return EXIT_FAILURE;
        }

        std::vector<std::uint8_t> encoded;
        if (!encoder.Encode(frame, encoded)) {
            std::cerr << "Opus encode failed\n";
            return EXIT_FAILURE;
        }

        if (!sender.Send(sequence++, frame, encoded)) {
            std::cerr << "UDP send failed\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "Sent " << sequence << " frames to " << host << ':' << port << '\n';
    return EXIT_SUCCESS;
}
