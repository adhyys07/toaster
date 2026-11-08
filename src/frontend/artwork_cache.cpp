#include "artwork_cache.h"
#include <SDL_image.h>

#include <cstdio>
#include <filesystem>

namespace emu::frontend {
namespace fs = std::filesystem;

ArtworkCache::ArtworkCache(SDL_Renderer* renderer)
    : renderer_(renderer) {
    constexpr int kFormats = IMG_INIT_PNG | IMG_INIT_JPG;
    ready_ = (IMG_Init(kFormats) & kFormats) == kFormats;
    if (!ready_) {
        std::fprintf(stderr, "Cover art unavailable: %s\n", IMG_GetError());
    }
}

ArtworkCache::~ArtworkCache() {
    clear();
    if (ready_) IMG_Quit();
}

SDL_Texture* ArtworkCache::get(const std::string& path) {
    if (!ready_ || path.empty()) return nullptr;
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

bool setCoverArt(const std::string& rom_path, const std::string& image_path,
                 std::string& error) {
    std::error_code ec;
    std::filesystem::path target(rom_path);
    target.replace_extension(".cover.png");

    std::filesystem::copy_file(
        image_path, target,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Could not write the cover: " + ec.message();
        return false;
    }
    error.clear();
    return true;
}


std::string findArtwork(const std::string& rom_path) {
    std::error_code ignored;
    const fs::path rom(rom_path);
    static const char* extensions[] = {".png",".jpg",".jpeg",".bmp"};

    // A cover the player picked from a screenshot wins over anything named
    // after the ROM or scraped into artwork/, which is the point of picking
    // one. setCoverArt writes exactly this path.
    {
        fs::path chosen = rom;
        chosen.replace_extension(".cover.png");
        if (fs::is_regular_file(chosen, ignored)) return chosen.string();
    }

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
