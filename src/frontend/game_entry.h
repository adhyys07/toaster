#pragma once

#include <filesystem>
#include <string>

namespace emu::frontend {

// How far this game actually gets. Ordered worst to best so the list
// can sort by it, and so "at least In-game" is a single comparison.
enum class Compat {
    Unknown = 0,
    Nothing,      // does not boot
    Loadable,     // boots to nothing useful
    Intro,        // reaches a title or intro
    InGame,       // plays, with problems
    Playable      // no significant problems
};

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
    long long last_played = 0;
    int play_count = 0;
    // Wall seconds across all sessions. Fast-forwarding through a grind
    // still costs the minutes it took, so this is real time rather than
    // emulated time.
    long long seconds_played = 0;
    Compat compat = Compat::Unknown;

    // Whether an .ini beside the ROM overrides any global setting.
    // Resolved once when the list is built, never in the renderer: the
    // row draw runs every frame and reading a file per row would be
    // absurd.
    bool custom_config = false;
};


} // namespace emu::frontend
