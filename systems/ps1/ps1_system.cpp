#include "ps1_system.h"

#include "../psp/libretro_host.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace emu::ps1 {
namespace {

namespace fs = std::filesystem;

fs::path executableDirectory() {
    char* base_path = SDL_GetBasePath();
    if (!base_path) return {};
    const fs::path result(base_path);
    SDL_free(base_path);
    return result;
}

fs::path resolvePath(const std::string& requested,
                     const fs::path& exe_dir,
                     bool directory) {
    const fs::path input(requested);
    std::vector<fs::path> candidates{input};
    if (!input.is_absolute() && !exe_dir.empty()) {
        candidates.push_back(exe_dir / input);
        candidates.push_back(exe_dir.parent_path() / input);
    }

    for (const fs::path& candidate : candidates) {
        std::error_code error;
        const bool exists = directory
            ? fs::is_directory(candidate, error)
            : fs::is_regular_file(candidate, error);
        if (exists && !error) {
            const fs::path absolute = fs::absolute(candidate, error);
            return error ? candidate : absolute;
        }
    }
    return {};
}

}  // namespace

int runPs1Game(const std::string& core_path,
               const std::string& game_path,
               const std::string& system_directory,
               bool fullscreen,
               bool mute,
               const emu::frontend::Settings& settings) {
    const fs::path exe_dir = executableDirectory();
    const fs::path core = resolvePath(core_path, exe_dir, false);
    const fs::path game = resolvePath(game_path, {}, false);
    const fs::path system = resolvePath(system_directory, exe_dir, true);

    if (core.empty()) {
        std::fprintf(stderr,
                     "PS1 error: Libretro PS1 core not found: %s\n",
                     core_path.c_str());
        return 1;
    }
    if (game.empty()) {
        std::fprintf(stderr, "PS1 error: game file not found: %s\n",
                     game_path.c_str());
        return 1;
    }
    if (system.empty()) {
        std::fprintf(stderr, "PS1 error: system directory not found: %s\n",
                     system_directory.c_str());
        return 1;
    }

    const fs::path bios = system / "swanstation" / "bios";
    if (!fs::is_directory(bios)) {
        std::fprintf(stderr,
            "PS1 error: place your legally dumped PS1 BIOS files in: %s\n",
            bios.string().c_str());
        return 1;
    }

    emu::libretro::LibretroHost host;
    std::string error;
    if (!host.run(core.string(), game.string(), system.string(), "PS1",
                  fullscreen, mute, settings, error)) {
        std::fprintf(stderr, "PS1 error: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

}  // namespace emu::ps1
