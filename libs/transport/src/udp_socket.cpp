#include "nspeaker/transport/udp_socket.h"

#include <array>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nspeaker::transport {
namespace {

#ifdef _WIN32
using SocketLength = int;
constexpr int kInvalidFd = INVALID_SOCKET;
#else
using SocketLength = socklen_t;
constexpr int kInvalidFd = -1;
#endif

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        initialized_ = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        if (initialized_) {
            WSACleanup();
        }
#endif
    }

private:
    bool initialized_ = true;
};

SocketRuntime& Runtime() {
    static SocketRuntime runtime;
    return runtime;
}

void CloseFd(int fd) noexcept {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

[[nodiscard]] bool FillSockaddr(const std::string& host, std::uint16_t port,
                                sockaddr_in& addr) {
    addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return false;
    }

    const auto* resolved = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    addr.sin_addr = resolved->sin_addr;
    freeaddrinfo(result);
    return true;
}

[[nodiscard]] std::string PeerToString(const sockaddr_in& addr) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    if (inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), static_cast<socklen_t>(buffer.size())) ==
        nullptr) {
        return {};
    }
    return {buffer.data()};
}

}  // namespace

UdpSocket::UdpSocket() {
    static_cast<void>(Runtime());
}

UdpSocket::~UdpSocket() {
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_), bound_port_(other.bound_port_) {
    other.fd_ = kInvalidFd;
    other.bound_port_ = 0;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        bound_port_ = other.bound_port_;
        other.fd_ = kInvalidFd;
        other.bound_port_ = 0;
    }
    return *this;
}

bool UdpSocket::Open() {
    return EnsureOpen();
}

bool UdpSocket::Bind(std::uint16_t port) {
    if (!EnsureOpen()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    const int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    if (bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        return false;
    }

    sockaddr_in bound{};
    SocketLength len = sizeof(bound);
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
        bound_port_ = ntohs(bound.sin_port);
    }
    return true;
}

bool UdpSocket::Connect(const std::string& host, std::uint16_t port) {
    if (!EnsureOpen()) {
        return false;
    }

    sockaddr_in addr{};
    if (!FillSockaddr(host, port, addr)) {
        return false;
    }

    return connect(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool UdpSocket::Send(const std::vector<std::uint8_t>& payload) {
    if (!EnsureOpen()) {
        return false;
    }

    return send(fd_, reinterpret_cast<const char*>(payload.data()),
                static_cast<int>(payload.size()), 0) == static_cast<int>(payload.size());
}

bool UdpSocket::SendTo(const std::string& host, std::uint16_t port,
                       const std::vector<std::uint8_t>& payload) {
    if (!EnsureOpen()) {
        return false;
    }

    sockaddr_in addr{};
    if (!FillSockaddr(host, port, addr)) {
        return false;
    }

    return sendto(fd_, reinterpret_cast<const char*>(payload.data()),
                  static_cast<int>(payload.size()), 0,
                  reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) == static_cast<int>(payload.size());
}

std::optional<Datagram> UdpSocket::Receive(std::chrono::milliseconds timeout) {
    if (!EnsureOpen()) {
        return std::nullopt;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(fd_, &read_set);

    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    const int ready = select(fd_ + 1, &read_set, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> buffer(2048);
    sockaddr_in peer{};
    SocketLength peer_len = sizeof(peer);
    const int received = recvfrom(fd_, reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (received <= 0) {
        return std::nullopt;
    }

    buffer.resize(static_cast<std::size_t>(received));
    return Datagram{
        .payload = std::move(buffer),
        .peer_address = PeerToString(peer),
        .peer_port = ntohs(peer.sin_port),
    };
}

std::uint16_t UdpSocket::bound_port() const noexcept {
    return bound_port_;
}

void UdpSocket::Close() noexcept {
    if (fd_ != kInvalidFd) {
        CloseFd(fd_);
        fd_ = kInvalidFd;
    }
    bound_port_ = 0;
}

bool UdpSocket::EnsureOpen() {
    if (fd_ != kInvalidFd) {
        return true;
    }

    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    return fd_ != kInvalidFd;
}

}  // namespace nspeaker::transport
