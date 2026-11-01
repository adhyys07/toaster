#include "core_manager.h"

#include <utility>

namespace emu::frontend {

CoreManager::CoreManager(std::filesystem::path root)
    : root_(std::move(root)) {}

std::vector<CoreInfo> CoreManager::discover() const {
    std::vector<CoreInfo> result;

    std::error_code error;
    if (!std::filesystem::is_directory(root_, error) || error)
        return result;

    for (const auto& entry :
         std::filesystem::directory_iterator(root_, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) || error) {
            error.clear();
            continue;
        }

        const auto name = entry.path().filename().string();

        CoreSystem system;
        if (name.find("swanstation_libretro") != std::string::npos)
            system = CoreSystem::Ps1;
        else if (name.find("ppsspp_libretro") != std::string::npos)
            system = CoreSystem::Psp;
        else if (name.find("pcsx2_libretro") != std::string::npos)
            system = CoreSystem::Ps2;
        else
            continue;

        result.push_back({system, entry.path(), name});
    }

    return result;
}

std::optional<CoreInfo> CoreManager::find(CoreSystem system) const {
    for (const auto& core : discover()) {
        if (core.system == system)
            return core;
    }

    return std::nullopt;
}

}