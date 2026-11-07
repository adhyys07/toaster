#pragma once

#include "../game_entry.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace emu::frontend::ui {

// Everything the screen needs from the rest of the frontend.
//
// The screen owns no business logic. The library service, the favourites
// file, the BIOS importer and the core installer all stay in main.cpp and are
// reached through these, so this file never has to know whether a library
// service is configured.
struct LibraryActions {
    // Picks up a finished background scan. True when the game list was
    // replaced, which is the screen's cue to repaint and re-clamp its
    // selection.
    std::function<bool()> collectScan;

    // True while a scan is in flight. GameLibrary is not thread safe, so the
    // screen hides the statistics line and refuses per-game actions until it
    // returns false.
    std::function<bool()> isScanning;

    std::function<void()> rescan;

    // False when the flag could not be persisted, so the screen can report it
    // rather than silently showing a favourite that will not survive a
    // restart.
    std::function<bool(const std::string& path, bool favorite)> setFavorite;

    std::function<void(const std::string& path)> markPlayed;

    // These still open their own windows. Converting them to in-window
    // overlays is the next step; the screen already dims and repaints
    // correctly when they return.
    std::function<void()> openSettings;
    std::function<void()> installPs1Bios;
    std::function<void()> installPs2Bios;
    std::function<void()> installCores;

    // Returns an empty string when the dialog was cancelled.
    std::function<std::string()> browseForGame;

    // The theme named in settings.ini, and a way to persist a new choice.
    // "custom" means read theme.ini.
    std::function<std::string()> themeName;
    std::function<void(const std::string& name)> setThemeName;

    // One short line for the footer, e.g. "Total 48  Plays 12". Empty hides
    // it.
    std::function<std::string()> statusLine;
};

// Runs the library until the user launches something or quits. Returns the
// chosen game's path, or an empty string on quit.
//
// `games` is read every frame and may be replaced underneath by a scan
// collected through LibraryActions::collectScan, so it is taken by reference
// rather than copied.
std::string runLibraryScreen(std::vector<GameEntry>& games,
                             const std::filesystem::path& folder,
                             const LibraryActions& actions);

} // namespace emu::frontend::ui
