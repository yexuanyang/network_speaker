#include "nspeaker/client/client_session.h"

#include "nspeaker/codec/opus_codec.h"

namespace nspeaker::client {

ClientSession::ClientSession(Config config, std::shared_ptr<audio::IAudioSink> sink,
                             std::shared_ptr<audio::Clock> clock)
    : config_(config), sink_(std::move(sink)), clock_(std::move(clock)) {}

ClientSession::~ClientSession() {
    Stop();
}

bool ClientSession::Start() {
    if (running()) {
        return true;
    }
    if (!receiver_.Bind(config_.listen_port)) {
        return false;
    }

    running_.store(true);
    worker_ = std::thread(&ClientSession::Run, this);
    return true;
}

void ClientSession::Stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool ClientSession::running() const noexcept {
    return running_.load();
}

audio::StreamStats ClientSession::stats() const {
    std::scoped_lock lock(mutex_);
    return stats_;
}

void ClientSession::Run() {
    auto decoder = std::make_unique<codec::OpusDecoder>();
    if (!decoder->ok()) {
        running_.store(false);
        return;
    }

    PlayerPipeline pipeline(std::move(decoder), sink_, clock_, config_.jitter_target_packets);
    while (running()) {
        if (auto packet = receiver_.PollOne(config_.poll_timeout, config_.allowed_sender_ipv4);
            packet.has_value()) {
            pipeline.PushPacket(std::move(*packet));
        }
        pipeline.DrainReady();

        std::scoped_lock lock(mutex_);
        stats_ = pipeline.stats();
    }
}

}  // namespace nspeaker::client
