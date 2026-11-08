#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace emu::frontend {

enum class LogLevel { Debug, Info, Warning, Error };

class LogBuffer {
public:
    static LogBuffer& instance();

    void push(LogLevel level, std::string message);

    struct Line {
        LogLevel level = LogLevel::Info;
        std::string text;
    };
    std::vector<Line> snapshot(LogLevel minimum) const;

    void clear();

private:
    static constexpr std::size_t kCapacity = 512;

    mutable std::mutex mutex_;
    std::array<Line, kCapacity> lines_;
    std::size_t next_ = 0;
    std::size_t filled_ = 0;
};

void logLine(LogLevel level, const std::string& message);

}