#include "artwork_cache.h"
#include <SDL_image.h>

#include <filesystem>

namespace emu::frontend {
namespace fs = std::filesystem;

SDL_Texture* ArtworkCache::get(const std::string& path) {
    if (path.empty()) return nullptr;
    const auto found = textures_.find(path);
    if (found != textures_.end()) return found->second;

    SDL_Texture* texture = IMG_LoadTexture(renderer_, path.c_str());
    if (!texture) {
        std::fprintf(stderr, "Could not load artwork '%s': %s\n",
                     path.c_str(), IMG_GetError());
    }
    textures_.emplace(path, texture);   // null is cached deliberately
    return texture;
}

void ArtworkCache::clear(){
    for (auto& entry : textures_)
        if (entry.second) SDL_DestroyTexture(entry.second);
    textures_.clear();
}

std::string findArtwork(const std::string& rom_path) {
    std::error_code ignored;
    const fs::path rom(rom_path);
    static const char* extensions[] = {".png",".jpg",".jpeg",".bmp"};

    for (const char* extension : extensions) {
        fs::path candidate = rom;
        candidate.replace_extension(extension);
        if (fs::is_regular_file(candidate, ignored)) return candidate.string();
    }
    for (const char* extension : extensions) {
        const fs::path candidate =
            rom.parent_path() / "artwork" / (rom.stem().string() + extension);
        if (fs::is_regular_file(candidate, ignored)) return candidate.string();
    }
    const fs::path folder = rom.parent_path() / "folder.png";
    if (fs::is_regular_file(folder, ignored)) return folder.string();
    return {};
}
} // namespace emu::frontend
