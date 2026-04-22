#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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
    std::cerr
        << "Usage: clientd --port <port> [options]\n"
           "\n"
           "Options:\n"
           "  --port <n>                    UDP listen port (default 50000)\n"
           "  --seconds <n>                 Run for N seconds (default 5)\n"
           "  --stats-interval <n>          Print stats every N seconds (default 1, 0=disable)\n"
           "  --steady-target-packets <n>   Jitter buffer target depth in packets (default 3)\n"
           "  --late-drop-threshold-ms <n>  Soft catch-up threshold ms (default 200, 0=disable)\n"
           "  --disable-fast-lock           Disable FastLock startup sequence jump\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 50000;
    int seconds = 5;
    int stats_interval = 1;

    nspeaker::client::PipelineConfig pipeline_config;  // defaults: FastLock on, target=3

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stoi(argv[++i]);
        } else if (arg == "--stats-interval" && i + 1 < argc) {
            stats_interval = std::stoi(argv[++i]);
        } else if (arg == "--steady-target-packets" && i + 1 < argc) {
            pipeline_config.steady_target_packets = static_cast<std::size_t>(std::stoi(argv[++i]));
        } else if (arg == "--late-drop-threshold-ms" && i + 1 < argc) {
            pipeline_config.late_frame_drop_threshold_ms =
                static_cast<std::uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--disable-fast-lock") {
            pipeline_config.startup_fast_lock_enabled = false;
        } else {
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    auto sink = std::make_shared<CountingSink>();
    nspeaker::client::ClientSession session({.listen_port = port,
                                             .pipeline_config = pipeline_config,
                                             .poll_timeout = std::chrono::milliseconds(20)},
                                            sink);

    if (!session.Start()) {
        std::cerr << "Failed to bind UDP receiver on port " << port << '\n';
        return EXIT_FAILURE;
    }

    // Background thread: print stats every stats_interval seconds.
    std::atomic<bool> printing{stats_interval > 0};
    std::thread stats_thread;
    if (stats_interval > 0) {
        stats_thread = std::thread([&]() {
            while (printing.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(stats_interval));
                if (!printing.load()) break;
                const auto s = session.stats();
                std::cout << "[stats] e2e=" << s.e2e_latency_ms
                          << "ms jitter_buf=" << s.current_jitter_ms
                          << "ms peak=" << s.peak_jitter_ms
                          << "ms jitter_var_us=" << s.jitter_variance_us
                          << " lost=" << s.packets_lost
                          << " startup_skip=" << s.startup_skipped_packets
                          << " late_drop=" << s.late_dropped << " underrun=" << s.playback_underruns
                          << '\n';
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    printing.store(false);
    session.Stop();
    if (stats_thread.joinable()) {
        stats_thread.join();
    }

    const auto stats = session.stats();
    std::cout << "Decoded frames=" << sink->frames() << " samples=" << sink->samples()
              << " latency_ms=" << stats.e2e_latency_ms
              << " peak_jitter_ms=" << stats.peak_jitter_ms
              << " jitter_var_us=" << stats.jitter_variance_us
              << " startup_skip=" << stats.startup_skipped_packets << '\n';
    return EXIT_SUCCESS;
}
