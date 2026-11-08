#include "log_buffer.h"

#include <cstdio>

namespace emu::frontend {
namespace {

const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:   return "debug";
    case LogLevel::Info:    return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error:   return "error";
    default:                return "";
    }
}

} // namespace

LogBuffer& LogBuffer::instance() {
    // ordering problem.
    static LogBuffer buffer;
    return buffer;
}

void LogBuffer::push(LogLevel level, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_[next_] = Line{level, std::move(message)};
    next_ = (next_ + 1) % kCapacity;
    if (filled_ < kCapacity) ++filled_;
}

std::vector<LogBuffer::Line> LogBuffer::snapshot(LogLevel minimum) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Line> out;
    out.reserve(filled_);
    for (std::size_t i = 0; i < filled_; ++i) {
        // Oldest first, left exactly as the ring holds them: next_ - filled_
        // (mod kCapacity) is the oldest surviving entry.
        const std::size_t index = (next_ + kCapacity - filled_ + i) % kCapacity;
        const Line& line = lines_[index];
        if (static_cast<int>(line.level) >= static_cast<int>(minimum)) {
            out.push_back(line);
        }
    }
    return out;
}

void LogBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    next_ = 0;
    filled_ = 0;
}

void logLine(LogLevel level, const std::string& message) {
    LogBuffer::instance().push(level, message);
    // Mirrored to stderr so piping the binary, or reading a captured log
    // after a crash, still works exactly as before this existed.
    std::fprintf(stderr, "[%s] %s\n", levelName(level), message.c_str());
}

} // namespace emu::frontend
