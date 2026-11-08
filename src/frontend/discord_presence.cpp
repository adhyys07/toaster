#include "discord_presence.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <SDL.h>

namespace emu::frontend {
namespace {

// Discord accepts one activity update every 15 seconds and ignores the rest,
// so sending faster only wastes work.
constexpr uint64_t kMinIntervalMs = 15000;

// Discord opens up to ten sockets, one per running client. The first that
// accepts a connection is the one to talk to.
constexpr int kMaxSocketIndex = 10;

// Frames are [int32 opcode][int32 payload length][payload], both counts
// little-endian. Anything longer than this is not something we would send.
constexpr std::size_t kMaxPayload = 64 * 1024;

void appendLe32(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

// Escapes a game title for embedding in JSON. Titles come from filenames, so
// quotes, backslashes and stray control bytes all turn up in practice and
// would otherwise produce a payload Discord silently rejects.
std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escape[8] = {};
                std::snprintf(escape, sizeof(escape), "\\u%04x",
                              static_cast<unsigned>(c));
                out += escape;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

int currentProcessId() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

} // namespace

DiscordPresence::~DiscordPresence() { close(); }

bool DiscordPresence::connect(const std::string& application_id) {
    close();
    // No application id means nothing to identify ourselves as, so there is
    // no point opening a socket.
    if (application_id.empty()) return false;
    application_id_ = application_id;

#ifdef _WIN32
    for (int index = 0; index < kMaxSocketIndex; ++index) {
        char name[64] = {};
        std::snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d",
                      index);
        const HANDLE handle =
            CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        pipe_ = reinterpret_cast<std::intptr_t>(handle);
        break;
    }
    if (pipe_ == -1) return false;
#else
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) runtime = std::getenv("TMPDIR");
    if (!runtime) runtime = "/tmp";

    for (int index = 0; index < kMaxSocketIndex; ++index) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::snprintf(address.sun_path, sizeof(address.sun_path),
                      "%s/discord-ipc-%d", runtime, index);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&address),
                      sizeof(address)) == 0) {
            pipe_ = static_cast<std::intptr_t>(fd);
            break;
        }
        ::close(fd);
    }
    if (pipe_ == -1) return false;
#endif

    // Opcode 0 is the handshake. Discord drops the connection if anything
    // else arrives first, so a failure here is a failure to connect.
    const std::string handshake =
        "{\"v\":1,\"client_id\":\"" + jsonEscape(application_id_) + "\"}";
    if (!writeFrame(0, handshake)) {
        close();
        return false;
    }

    connected_ = true;
    return true;
}

bool DiscordPresence::writeFrame(int opcode, const std::string& payload) {
    if (pipe_ == -1 || payload.size() > kMaxPayload) return false;

    std::string frame;
    frame.reserve(8 + payload.size());
    appendLe32(frame, static_cast<uint32_t>(opcode));
    appendLe32(frame, static_cast<uint32_t>(payload.size()));
    frame += payload;

#ifdef _WIN32
    const HANDLE handle = reinterpret_cast<HANDLE>(pipe_);
    DWORD written = 0;
    if (!WriteFile(handle, frame.data(),
                   static_cast<DWORD>(frame.size()), &written, nullptr)) {
        return false;
    }
    return written == frame.size();
#else
    std::size_t sent = 0;
    while (sent < frame.size()) {
        // MSG_NOSIGNAL: a Discord that quit mid-write would otherwise raise
        // SIGPIPE and take the emulator down with it.
        const ssize_t wrote =
            ::send(static_cast<int>(pipe_), frame.data() + sent,
                   frame.size() - sent, MSG_NOSIGNAL);
        if (wrote <= 0) return false;
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
#endif
}

void DiscordPresence::setGame(const std::string& title,
                              const std::string& system,
                              int64_t started_at_unix) {
    if (!connected_) return;

    std::string payload;
    payload.reserve(256);
    payload += "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":";
    payload += std::to_string(currentProcessId());
    payload += ",\"activity\":{\"details\":\"";
    payload += jsonEscape(title);
    payload += "\",\"state\":\"";
    payload += jsonEscape(system);
    payload += "\"";
    if (started_at_unix > 0) {
        // A start timestamp is what makes Discord show a running clock rather
        // than a static line.
        payload += ",\"timestamps\":{\"start\":";
        payload += std::to_string(started_at_unix);
        payload += "}";
    }
    payload += "}},\"nonce\":\"";
    payload += std::to_string(started_at_unix);
    payload += "\"}";

    const uint64_t now = SDL_GetTicks64();
    // An unchanged payload is dropped whatever the timing, so pausing and
    // resuming the same game does not burn the rate limit.
    if (payload == last_payload_) return;
    if (last_sent_ms_ != 0 && now - last_sent_ms_ < kMinIntervalMs) return;

    if (!writeFrame(1, payload)) {
        // Discord quitting is the normal way this fails. Dropping the
        // connection is correct; reconnecting is the caller's business.
        close();
        return;
    }
    last_payload_ = payload;
    last_sent_ms_ = now;
}

void DiscordPresence::clear() {
    if (!connected_) return;
    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":";
    payload += std::to_string(currentProcessId());
    payload += ",\"activity\":null},\"nonce\":\"clear\"}";
    // Sent unconditionally: clearing is what runs when a game closes, and
    // leaving a stale "playing" line up is worse than exceeding the limit.
    writeFrame(1, payload);
    last_payload_.clear();
}

void DiscordPresence::close() {
    if (pipe_ != -1) {
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(pipe_));
#else
        ::close(static_cast<int>(pipe_));
#endif
        pipe_ = -1;
    }
    connected_ = false;
    last_payload_.clear();
    last_sent_ms_ = 0;
}

} // namespace emu::frontend
