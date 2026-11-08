#include "link_cable.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace emu::frontend {
namespace {

#ifdef _WIN32
constexpr socket_t kBadSocket = INVALID_SOCKET;
void closeSocket(socket_t s) { closesocket(s); }
std::string socketError() { return std::to_string(WSAGetLastError()); }
#else
constexpr socket_t kBadSocket = -1;
void closeSocket(socket_t s) { ::close(s); }
std::string socketError() { return std::strerror(errno); }
#endif

int waitReadable(socket_t s, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    return select(0, &set, nullptr, nullptr, &timeout);
#else
    return select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &timeout);
#endif
}

void disableNagle(socket_t s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&one), sizeof(one));
}

} // namespace

LinkCable::~LinkCable() { close(); }

bool LinkCable::startup(std::string& error) {
#ifdef _WIN32
    if (!initialized_) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "Could not start Windows sockets.";
            return false;
        }
        initialized_ = true;
    }
#else
    (void)error;
    initialized_ = true;
#endif
    return true;
}

bool LinkCable::host(uint16_t port, int timeout_ms, std::string& error) {
    close();
    if (!startup(error)) return false;

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kBadSocket) {
        error = "Could not open a socket: " + socketError();
        return false;
    }

    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listener, 1) != 0) {
        error = "Could not listen on port " + std::to_string(port) + ": " +
                socketError();
        closeSocket(listener);
        return false;
    }

    const int ready = waitReadable(listener, timeout_ms);
    if (ready <= 0) {
        error = ready == 0 ? "No one connected in time."
                           : "Waiting for a connection failed: " +
                                 socketError();
        closeSocket(listener);
        return false;
    }

    const socket_t peer = ::accept(listener, nullptr, nullptr);
    closeSocket(listener);
    if (peer == kBadSocket) {
        error = "Could not accept the connection: " + socketError();
        return false;
    }

    disableNagle(peer);
    peer_ = static_cast<std::intptr_t>(peer);
    listener_ = kInvalid;
    error.clear();
    return true;
}

bool LinkCable::join(const std::string& address, uint16_t port,
                     int timeout_ms, std::string& error) {
    close();
    if (!startup(error)) return false;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* resolved = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(address.c_str(), service.c_str(), &hints, &resolved) != 0 ||
        !resolved) {
        error = "Could not resolve " + address + ".";
        return false;
    }

    const socket_t peer = ::socket(resolved->ai_family, SOCK_STREAM, 0);
    if (peer == kBadSocket) {
        freeaddrinfo(resolved);
        error = "Could not open a socket: " + socketError();
        return false;
    }

    (void)timeout_ms;
    const int result = ::connect(peer, resolved->ai_addr,
                                 static_cast<int>(resolved->ai_addrlen));
    freeaddrinfo(resolved);

    if (result != 0) {
        closeSocket(peer);
        error = "Could not reach " + address + ": " + socketError();
        return false;
    }

    disableNagle(peer);
    peer_ = static_cast<std::intptr_t>(peer);
    error.clear();
    return true;
}

uint8_t LinkCable::exchange(uint8_t outgoing) {
    if (peer_ == kInvalid) return 0xFF;

    const socket_t peer = static_cast<socket_t>(peer_);
    const char sent = static_cast<char>(outgoing);

    if (::send(peer, &sent, 1, 0) != 1) {
        last_error_ = "The link partner stopped listening.";
        close();
        return 0xFF;
    }

    const int ready = waitReadable(peer, kExchangeTimeoutMs);
    if (ready <= 0) {
        last_error_ = ready == 0 ? "The link partner did not answer in time."
                                 : "The link connection failed.";
        close();
        return 0xFF;
    }

    char received = 0;
    const int got = static_cast<int>(::recv(peer, &received, 1, 0));
    if (got != 1) {
        last_error_ = "The link partner disconnected.";
        close();
        return 0xFF;
    }

    return static_cast<uint8_t>(received);
}

void LinkCable::close() {
    if (peer_ != kInvalid) {
        closeSocket(static_cast<socket_t>(peer_));
        peer_ = kInvalid;
    }
    if (listener_ != kInvalid) {
        closeSocket(static_cast<socket_t>(listener_));
        listener_ = kInvalid;
    }
#ifdef _WIN32
    if (initialized_) {
        WSACleanup();
        initialized_ = false;
    }
#endif
}

} // namespace emu::frontend
