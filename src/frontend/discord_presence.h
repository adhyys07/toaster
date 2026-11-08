#pragma once

#include <cstdint>
#include <string>

namespace emu::frontend {

// Publishes "playing X" to a Discord client running on this machine, over its
// local IPC socket.
//
// Entirely optional and entirely silent on failure: no Discord, no socket, no
// problem. It never blocks the frame loop either - a write that would block is
// dropped, because a social nicety must not cost a frame.
class DiscordPresence {
public:
    DiscordPresence() = default;
    ~DiscordPresence();

    // The socket handle is owned, so copying one would close it twice.
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // `application_id` comes from the Discord developer portal. Without one
    // there is nothing to connect to, so this returns false quietly.
    bool connect(const std::string& application_id);
    bool connected() const { return connected_; }

    // Rate-limited internally to one update every 15 seconds, which is what
    // Discord accepts; calls in between are dropped rather than queued, and
    // an unchanged payload is dropped whatever the timing.
    void setGame(const std::string& title, const std::string& system,
                 int64_t started_at_unix);

    void clear();
    void close();

private:
    bool writeFrame(int opcode, const std::string& payload);

    // Platform handle: a HANDLE on Windows, a socket descriptor elsewhere.
    // Kept as a pointer-sized integer so this header needs no platform
    // headers of its own.
    std::intptr_t pipe_ = -1;
    bool connected_ = false;
    std::string application_id_;
    std::string last_payload_;
    uint64_t last_sent_ms_ = 0;
};

} // namespace emu::frontend
