#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "nspeaker/audio/clock.h"
#include "nspeaker/audio/sink.h"
#include "nspeaker/audio/stream_stats.h"
#include "nspeaker/client/player_pipeline.h"
#include "nspeaker/client/receiver.h"

namespace nspeaker::client {

class ClientSession {
public:
    struct Config {
        std::uint16_t listen_port = 50000;
        std::string allowed_sender_ipv4;
        PipelineConfig pipeline_config;
        std::chrono::milliseconds poll_timeout{20};
    };

    ClientSession(Config config, std::shared_ptr<audio::IAudioSink> sink,
                  std::shared_ptr<audio::Clock> clock = std::make_shared<audio::SteadyClock>());
    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    [[nodiscard]] bool Start();
    void Stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] audio::StreamStats stats() const;

private:
    void Run();

    Config config_;
    std::shared_ptr<audio::IAudioSink> sink_;
    std::shared_ptr<audio::Clock> clock_;
    Receiver receiver_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    audio::StreamStats stats_{};
};

}  // namespace nspeaker::client
