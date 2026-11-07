#pragma once

#include "core/system.h"

#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

std::filesystem::path screenshotsDirectory();

bool savePngTo(const VideoFrame& frame,
               const std::filesystem::path& path,
               std::string& error);

std::string saveScreenshot(const VideoFrame& frame,
                           const std::string& game_path,
                           std::string& error);

std::vector<std::filesystem::path> listScreenshots(
    const std::string& game_path);

std::vector<std::filesystem::path> listAllScreenshots();

} // namespace emu::frontend
