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

    // Unix seconds, 0 for never. The library service has carried this in
    // RemoteGame::last_played all along; the local scan keeps it in
    // recent.ini. Without it the Recent chip can only show everything.
    long long last_played = 0;
    int play_count = 0;
};

} // namespace emu::frontend
