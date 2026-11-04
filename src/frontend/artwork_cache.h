#pragma once
 #include <SDL.h>
 #include <string>
 #include <unordered_map>

 namespace emu::frontend {

class ArtworkCache() {

public:
    explicit ArtworkCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~ArtworkCache() { clear(); }

    ArtworkCache(const ArtworkCache&) = delete;
    ArtworkCache& operator=(const ArtworkCache&) = delete;

    SDL_Texture* get(const std::string& path);
    void clear();

private:
    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, SDL_Texture*> textures_;
};

std::string findArtwork(const std::string& rom_path);

}