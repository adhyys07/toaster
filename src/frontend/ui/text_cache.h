#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace emu::frontend::ui
{

enum class FontRole { Body, BodyBold, Title, Small, Count };

class TextCache {
public:
    TextCache() = default;
    ~TextCache();

    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;

    bool open(float scale, std::string& error);
    void close();

    bool rescale(float scale, std::string& error);

    TTF_Font* font(FontRole role) const;
    int lineHeight(FontRole role) const;

    void measure(FontRole role, const std::string& text,
                 int* width, int* height) const;
    int width(FontRole role, const std::string& text) const;

    int draw(SDL_Renderer* renderer, FontRole role, const std::string& text, int x, int y, SDL_Color color, int wrap_px = 0);

    void drawCentered(SDL_Renderer* renderer, FontRole role,
                      const std::string& text, const SDL_Rect& rect,
                      SDL_Color color);

    void drawRight(SDL_Renderer* renderer, FontRole role,
                   const std::string& text, int right_x, int y,
                   SDL_Color color);

    std::string ellipsize(FontRole role, const std::string& text,
                          int max_px) const;
    
    void endFrame();

    std::size_t entryCount() const { return entries_.size(); }

private:
    struct Key {
        FontRole role;
        std::uint32_t color;
        int wrap;
        std::string text;

        bool operator==(const Key& other) const {
            return role == other.role && color == other.color &&
                   wrap == other.wrap && text == other.text;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    struct Entry
    {
        SDL_Texture* texture = nullptr;
        int w = 0;
        int h = 0;
        std::uint64_t last_used = 0;
    };

    Entry* acquire(SDL_Renderer* renderer, const Key& key);

    TTF_Font* fonts_[static_cast<int>(FontRole::Count)]{};
    float scale_ = 1.0f;
    std::unordered_map<Key, Entry, KeyHash> entries_;
    std::uint64_t frame_ = 0;

    static constexpr std::uint64_t kEvictAfterFrames = 600;
    static constexpr std::size_t kSoftLimit = 1024;

};

} 