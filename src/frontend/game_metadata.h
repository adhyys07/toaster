#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

namespace emu::frontend {

struct GameMetadata {
    std::string title;
    std::string system;
    std::filesystem::path path;
    bool favorite = false;
    uint64_t last_played = 0;
};

GameMetadata detectGameMetadata(
    const std::filesystem::path& path);

bool saveGameMetadata(
    const GameMetadata& metadata,
    const std::filesystem::path& file);

bool loadGameMetadata(
    const std::filesystem::path& file,
    GameMetadata& metadata);

}