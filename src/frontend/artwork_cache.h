#pragma once

#include <SDL.h>

#include <string>
#include <unordered_map>

namespace emu::frontend {

class ArtworkCache {
public:
    explicit ArtworkCache(SDL_Renderer* renderer);
    ~ArtworkCache();

    ArtworkCache(const ArtworkCache&) = delete;
    ArtworkCache& operator=(const ArtworkCache&) = delete;

    // False when PNG and JPEG support could not be loaded.
    bool ready() const { return ready_; }

    SDL_Texture* get(const std::string& path);
    void clear();


private:
    SDL_Renderer* renderer_ = nullptr;
    bool ready_ = false;
    std::unordered_map<std::string, SDL_Texture*> textures_;
};

std::string findArtwork(const std::string& rom_path);
bool setCoverArt(const std::string& rom_path, const std::string& image_path, std::string&error);


}