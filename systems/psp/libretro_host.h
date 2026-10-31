#pragma once

#include <string>

namespace emu::psp {

class LibretroHost {
public:
    bool run(const std::string& core_path,
             const std::string& game_path,
             const std::string& system_directory,
             bool fullscreen,
             bool mute,
             std::string& error);
};

}  // namespace emu::psp
