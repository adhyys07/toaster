#pragma once

#include <string>

namespace emu::ps2 {
    int runPs2Game(const std::string& core_path,
               const std::string& game_path,
               const std::string& system_directory,
               bool fullscreen,
               bool mute);
}