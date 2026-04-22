#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "nspeaker/codec/opus_codec.h"
#include "nspeaker/server/capture_factory.h"
#include "nspeaker/server/platform/windows/wasapi_device_enumerator.h"
#include "nspeaker/server/udp_audio_sender.h"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void OnSignal(int /*signal*/) {
    g_stop_requested = 1;
}

void PrintUsage() {
    std::cerr << "Usage: hostd --host <ip> --port <port> [--source sine|pulse|wasapi]"
              << " [--pulse-source <name>] [--wasapi-role auto|multimedia|console|communications]"
              << " [--device <endpoint-id>] [--seconds <n>]\n"
              << "       hostd --list-devices\n";
}

std::string EscapeJsonString(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                result += buf;
            } else {
                result += ch;
            }
            break;
        }
    }
    return result;
}

void PrintDevicesJson(const std::vector<nspeaker::server::AudioDeviceInfo>& devices) {
#ifdef _WIN32
    // Set console output to UTF-8 for proper Unicode support
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << '[';
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) {
            std::cout << ',';
        }
        std::cout << "{\"id\":\"" << EscapeJsonString(devices[i].id) << "\",\"name\":\""
                  << EscapeJsonString(devices[i].name) << "\",\"description\":\""
                  << EscapeJsonString(devices[i].description)
                  << "\",\"default\":" << (devices[i].is_default ? "true" : "false") << '}';
    }
    std::cout << "]\n";
}

std::uint32_t MakeStreamId() {
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto mixed =
        static_cast<std::uint64_t>(now_us) ^ (static_cast<std::uint64_t>(now_us) >> 32U);
    const auto stream_id = static_cast<std::uint32_t>(mixed & 0xFFFFFFFFU);
    return stream_id == 0 ? 1U : stream_id;
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 50000;
    nspeaker::server::CaptureConfig capture_config{};
    std::optional<int> seconds;
    bool list_devices = false;

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
        } else if (arg == "--wasapi-role" && i + 1 < argc) {
            capture_config.wasapi_role = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            capture_config.wasapi_device_id = argv[++i];
        } else if (arg == "--list-devices") {
            list_devices = true;
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

    if (list_devices) {
        const auto devices = nspeaker::server::EnumerateAudioRenderDevices();
        PrintDevicesJson(devices);
        return EXIT_SUCCESS;
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

    nspeaker::server::UdpAudioSender sender(host, port, MakeStreamId());
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
    std::cout.flush();

    std::uint32_t sequence = 0;
    std::uint32_t send_count = 0;
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

        ++send_count;
        if (send_count % 100 == 0) {
            std::cout << "[hostd] Sent " << send_count << " frames to " << host << ':' << port
                      << '\n';
            std::cout.flush();
        }
    }

    std::cout << "Sent " << sequence << " frames to " << host << ':' << port << '\n';
    return EXIT_SUCCESS;
}
