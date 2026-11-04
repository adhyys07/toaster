#include "game_metadata.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

namespace emu::frontend {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

}

GameMetadata detectGameMetadata(const std::filesystem::path& path) {
    GameMetadata metadata;
    metadata.path = path;
    metadata.title = path.stem().string();

    const std::string extension = lowerExtension(path);
    if (extension == ".nes") metadata.system = "NES";
    else if (extension == ".gb") metadata.system = "Game Boy";
    else if (extension == ".cue" || extension == ".bin" ||
             extension == ".chd") metadata.system = "PS1";
    else if (extension == ".iso" || extension == ".cso" ||
             extension == ".pbp" || extension == ".elf") {
        metadata.system = "Unknown";
    } else {
        metadata.system = "Unknown";
    }
    return metadata;
}

bool saveGameMetadata(const GameMetadata& metadata,
                      const std::filesystem::path& file_path) {
    std::error_code error;
    const auto parent = file_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }

    std::ofstream file(file_path, std::ios::trunc);
    if (!file) return false;
    file << "title=" << metadata.title << '\n'
         << "system=" << metadata.system << '\n'
         << "path=" << metadata.path.string() << '\n'
         << "favorite=" << (metadata.favorite ? "true" : "false") << '\n'
         << "last_played=" << metadata.last_played << '\n';
    return file.good();
}

bool loadGameMetadata(const std::filesystem::path& file_path,
                      GameMetadata& metadata) {
    std::ifstream file(file_path);
    if (!file) return false;

    // Seed from the caller's value so a sidecar that sets only some keys
    // overlays them instead of blanking the rest. Without this, a .meta file
    // with no "path" line leaves the metadata with an empty path.
    GameMetadata loaded = metadata;
    std::string line;
    while (std::getline(file, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        try {
            if (key == "title") loaded.title = value;
            else if (key == "system") loaded.system = value;
            else if (key == "path") loaded.path = value;
            else if (key == "favorite") {
                loaded.favorite = value == "true" || value == "1";
            } else if (key == "last_played") {
                loaded.last_played = std::stoull(value);
            }
        } catch (const std::exception&) {
            return false;
        }
    }
    if (!file.good() && !file.eof()) return false;
    metadata = std::move(loaded);
    return true;
}

}
