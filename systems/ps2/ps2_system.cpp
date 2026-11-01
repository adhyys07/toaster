#include "ps2_system.h"

#include "libretro_host.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace emu::ps2 {
namespace {

std::filesystem::path executableDirectory() {
    char* base_path = SDL_GetBasePath();
    if (!base_path) return {};
    const std::filesystem::path result(base_path);
    SDL_free(base_path);
    return result;
}

std::filesystem::path resolvePath(const std::string& requested,
                                  const std::filesystem::path& exe_dir,
                                  bool directory) {
    const std::filesystem::path input(requested);
    std::vector<std::filesystem::path> candidates{input};
    if (!input.is_absolute() && !exe_dir.empty()) {
        candidates.push_back(exe_dir / input);
        candidates.push_back(exe_dir.parent_path() / input);
    }

    for (const auto& candidate : candidates) {
        std::error_code error;
        const bool exists = directory
            ? std::filesystem::is_directory(candidate, error)
            : std::filesystem::is_regular_file(candidate, error);
        if (exists && !error) {
            const auto absolute = std::filesystem::absolute(candidate, error);
            return error ? candidate : absolute;
        }
    }
    return {};
}

} // namespace

int runPs2Game(const std::string& core_path,
               const std::string& game_path,
               const std::string& system_directory,
               bool fullscreen,
               bool mute,
               const emu::frontend::Settings& settings) {
    namespace fs = std::filesystem;

    const fs::path exe_dir = executableDirectory();
    const fs::path resolved_core = resolvePath(core_path, exe_dir, false);
    const fs::path resolved_game = resolvePath(game_path, {}, false);
    const fs::path resolved_system = resolvePath(system_directory, exe_dir, true);

    if (resolved_core.empty()) {
        std::fprintf(stderr, "PS2 error: LRPS2 core not found: %s\n",
                     core_path.c_str());
        return 1;
    }
    if (resolved_game.empty()) {
        std::fprintf(stderr, "PS2 error: game file not found: %s\n",
                     game_path.c_str());
        return 1;
    }
    if (resolved_system.empty()) {
        std::fprintf(stderr, "PS2 error: system directory not found: %s\n",
                     system_directory.c_str());
        return 1;
    }

    const fs::path bios_directory = resolved_system / "pcsx2" / "bios";
    if (!fs::is_directory(bios_directory)) {
        std::fprintf(stderr,
            "PS2 error: place your legally dumped PS2 BIOS files in: %s\n",
            bios_directory.string().c_str());
        return 1;
    }

    // LRPS2 can treat an absolute Windows ELF path as relative to its ELF
    // directory. Run with the game's directory as CWD and give the core only
    // the filename so its own path resolution stays inside that directory.
    std::error_code cwd_error;
    const fs::path original_directory = fs::current_path(cwd_error);
    if (cwd_error) {
        std::fprintf(stderr, "PS2 error: cannot get current directory: %s\n",
                     cwd_error.message().c_str());
        return 1;
    }

    fs::current_path(resolved_game.parent_path(), cwd_error);
    if (cwd_error) {
        std::fprintf(stderr, "PS2 error: cannot enter game directory: %s\n",
                     cwd_error.message().c_str());
        return 1;
    }

    emu::libretro::LibretroHost host;
    std::string error;
    const bool loaded = host.run(
        resolved_core.string(),
        resolved_game.filename().string(),
        resolved_system.string(), "PS2", fullscreen, mute, settings, error);

    std::error_code restore_error;
    fs::current_path(original_directory, restore_error);

    if (!loaded) {
        std::fprintf(stderr, "PS2 error: %s\n", error.c_str());
        return 1;
    }
    if (restore_error) {
        std::fprintf(stderr,
                     "PS2 warning: could not restore the original working directory: %s\n",
                     restore_error.message().c_str());
        return 1;
    }
    return 0;
}

} // namespace emu::ps2
