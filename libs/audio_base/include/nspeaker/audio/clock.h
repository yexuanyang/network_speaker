#pragma once

#include <chrono>
#include <cstdint>

namespace nspeaker::audio {

class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual std::uint64_t NowMicros() const noexcept = 0;
};

class SteadyClock final : public Clock {
public:
    [[nodiscard]] std::uint64_t NowMicros() const noexcept override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    }
};

}  // namespace nspeaker::audio
