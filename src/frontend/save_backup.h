#pragma once

#include <filesystem>
#include <string>

namespace emu::frontend {

bool rotateBackups(const std::filesystem::path& file, int keep,
                   std::string& error);

}