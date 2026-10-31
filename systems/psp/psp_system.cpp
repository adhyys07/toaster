#include "psp_system.h"

#include "libretro_host.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace emu::psp {
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
                                  bool require_directory) {
    const std::filesystem::path input(requested);
    std::vector<std::filesystem::path> candidates;
    if (input.is_absolute()) {
        candidates.push_back(input);
    } else {
        candidates.push_back(input);
        if (!exe_dir.empty()) {
            candidates.push_back(exe_dir / input);
            candidates.push_back(exe_dir.parent_path() / input);
        }
    }

    for (const auto& candidate : candidates) {
        std::error_code error;
        const bool exists = require_directory
            ? std::filesystem::is_directory(candidate, error)
            : std::filesystem::is_regular_file(candidate, error);
        if (exists && !error) {
            const auto absolute = std::filesystem::absolute(candidate, error);
            return error ? candidate : absolute;
        }
    }
    return {};
}

}  // namespace

int runPspGame(const std::string& core_path,
               const std::string& game_path,
               const std::string& system_directory,
               bool fullscreen,
               bool mute) {
    const std::filesystem::path exe_dir = executableDirectory();
    const std::filesystem::path resolved_system =
        resolvePath(system_directory, exe_dir, true);
    if (resolved_system.empty() ||
        !std::filesystem::is_directory(resolved_system / "PPSSPP")) {
        const std::filesystem::path attempted = resolved_system.empty()
            ? std::filesystem::path(system_directory) / "PPSSPP"
            : resolved_system / "PPSSPP";
        std::fprintf(stderr,
            "PSP error: PPSSPP data folder was not found at '%s'. ",
            attempted.string().c_str());
        std::fprintf(stderr,
            "Copy the contents of PPSSPP's assets folder into system/PPSSPP.\n");
        return 1;
    }

    const std::filesystem::path resolved_core =
        resolvePath(core_path, exe_dir, false);
    if (resolved_core.empty()) {
        std::fprintf(stderr,
            "PSP error: PPSSPP Libretro core was not found at '%s'. Pass its location with --ppsspp-core.\n",
            core_path.c_str());
        return 1;
    }

    LibretroHost host;
    std::string error;
    if (!host.run(resolved_core.string(), game_path, resolved_system.string(),
                  fullscreen, mute, error)) {
        std::fprintf(stderr, "PSP error: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

}  // namespace emu::psp
