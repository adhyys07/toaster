#pragma once

#include <filesystem>
#include <string>

namespace emu::frontend {

class CrashLogger {
public:
    explicit CrashLogger(std::filesystem::path log_file);

    void write(const std::string& message);
    void writeGameStart(const std::string& core,
                        const std::string& game,
                        const std::string& system);

private:
    std::filesystem::path log_file_;
};

}