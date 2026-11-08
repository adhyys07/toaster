#pragma once

#include <cstdint>
#include <string>

namespace emu::frontend {

class LinkCable {
public:
    LinkCable() = default;
    ~LinkCable();

    LinkCable(const LinkCable&) = delete;
    LinkCable& operator=(const LinkCable&) = delete;

    bool host(uint16_t port, int timeout_ms, std::string& error);

    // Connects to a waiting host.
    bool join(const std::string& address, uint16_t port, int timeout_ms,
              std::string& error);

    void close();
    bool connected() const { return peer_ != kInvalid; }

    uint8_t exchange(uint8_t outgoing);

    const std::string& lastError() const { return last_error_; }

private:
    static constexpr int kExchangeTimeoutMs = 16;
    static constexpr std::intptr_t kInvalid = -1;

    bool startup(std::string& error);

    std::intptr_t listener_ = kInvalid;
    std::intptr_t peer_ = kInvalid;
    std::string last_error_;
    bool initialized_ = false;
};

} // namespace emu::frontend
