#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <string>

#include "nspeaker/audio/sink.h"
#include "nspeaker/client/client_session.h"

namespace {

class CountingSink final : public nspeaker::audio::IAudioSink {
public:
    bool SubmitPcm(const nspeaker::audio::PcmFrame& frame) override {
        ++frames_;
        samples_ += frame.samples_per_channel;
        return true;
    }

    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }
    [[nodiscard]] std::size_t samples() const noexcept { return samples_; }

private:
    std::size_t frames_ = 0;
    std::size_t samples_ = 0;
};

void PrintUsage() {
    std::cerr << "Usage: clientd --port <port> [--seconds <n>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 50000;
    int seconds = 5;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stoi(argv[++i]);
        } else {
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    auto sink = std::make_shared<CountingSink>();
    nspeaker::client::ClientSession session(
        {.listen_port = port,
         .allowed_sender_ipv4 = "",
         .jitter_target_packets = 6,
         .poll_timeout = std::chrono::milliseconds(20)},
        sink);
    if (!session.Start()) {
        std::cerr << "Failed to bind UDP receiver on port " << port << '\n';
        return EXIT_FAILURE;
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    session.Stop();
    const auto stats = session.stats();
    std::cout << "Decoded frames=" << sink->frames() << " samples=" << sink->samples()
              << " latency_ms=" << stats.e2e_latency_ms << '\n';
    return EXIT_SUCCESS;
}
