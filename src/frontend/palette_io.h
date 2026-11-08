#pragma once

#include "core/system.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

struct Palette {
    std::string name;
    std::vector<uint32_t> entries;   // ARGB
};

std::vector<Palette> builtinPalettes(const char* system_name, int size);

bool loadPalette(const std::filesystem::path& file, int size,
                 Palette& out, std::string& error);

std::vector<Palette> availablePalettes(const char* system_name, int size);

bool applyPalette(System& system, const std::string& name, std::string& error);

}