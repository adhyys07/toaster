#include "crash_logger.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <utility>

namespace emu::frontend {

CrashLogger::CrashLogger(std::filesystem::path log_file)
    : log_file_(std::move(log_file)) {}

void CrashLogger::write(const std::string& message) {
    std::error_code error;
    std::filesystem::create_directories(log_file_.parent_path(), error);

    std::ofstream file(log_file_, std::ios::app);
    if (!file) return;

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    file << "\n[" << std::put_time(
        std::localtime(&time), "%Y-%m-%d %H:%M:%S")
         << "]\n"
         << message << '\n';
}

void CrashLogger::writeGameStart(
    const std::string& core,
    const std::string& game,
    const std::string& system) {
    write("System: " + system +
          "\nCore: " + core +
          "\nGame: " + game);
}

}