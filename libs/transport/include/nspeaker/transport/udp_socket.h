#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nspeaker::transport {

struct Datagram {
    std::vector<std::uint8_t> payload;
    std::string peer_address;
    std::uint16_t peer_port = 0;
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    [[nodiscard]] bool Open();
    [[nodiscard]] bool Bind(std::uint16_t port);
    [[nodiscard]] bool Connect(const std::string& host, std::uint16_t port);
    [[nodiscard]] bool Send(const std::vector<std::uint8_t>& payload);
    [[nodiscard]] bool SendTo(const std::string& host, std::uint16_t port,
                              const std::vector<std::uint8_t>& payload);
    [[nodiscard]] std::optional<Datagram> Receive(std::chrono::milliseconds timeout);
    [[nodiscard]] std::uint16_t bound_port() const noexcept;

private:
    void Close() noexcept;
    [[nodiscard]] bool EnsureOpen();

    std::intptr_t fd_ = -1;
    std::uint16_t bound_port_ = 0;
};

}  // namespace nspeaker::transport
