#pragma once

#include <string>

namespace emu::libretro {

class LibretroHost {
public:
    bool run(const std::string& core_path,
             const std::string& game_path,
             const std::string& system_directory,
             const std::string& system_name,
             bool fullscreen,
             bool mute,
             std::string& error);
};

}  // namespace emu::libretro
