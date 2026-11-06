#pragma once

#include <filesystem>
#include <string>

namespace emu::frontend {

// One row of the game library.
//
// This lived in main.cpp's anonymous namespace, which meant nothing outside
// that translation unit could name it. The library screen needs it, so it
// moved here rather than being duplicated or passed around as a template
// parameter forever.
struct GameEntry {
    std::filesystem::path path;
    std::string title;
    std::string developer = "Unknown";
    std::string year = "Unknown";
    std::string genre = "Unknown";
    std::string description = "No description provided.";
    std::string artwork;
    std::string tags;
    bool favorite = false;
};

} // namespace emu::frontend
