#pragma once

#include <string>

namespace emu::psp {

int runPspGame(const std::string& core_path,
               const std::string& game_path,
               const std::string& system_directory,
               bool fullscreen,
               bool mute);

}  // namespace emu::psp
