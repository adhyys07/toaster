#include "text_cache.h"

#include "../ui_font.h"

#include <algorithm>
#include <cmath>

namespace emu::frontend::ui {
namespace {

std::uint32_t packColor(SDL_Color color) {
    return (static_cast<std::uint32_t>(color.r) << 24) |
           (static_cast<std::uint32_t>(color.g) << 16) |
           (static_cast<std::uint32_t>(color.b) << 8) |
           static_cast<std::uint32_t>(color.a);
}

SDL_Color unpackColor(std::uint32_t packed) {
    return SDL_Color{static_cast<Uint8>((packed >> 24) & 0xFFu),
                     static_cast<Uint8>((packed >> 16) & 0xFFu),
                     static_cast<Uint8>((packed >> 8) & 0xFFu),
                     static_cast<Uint8>(packed & 0xFFu)};
}

int pointSize(FontRole role) {
    switch(role) {
    case FontRole::Title: return 21;
    case FontRole::Small: return 12;
    case FontRole::Body:
    case FontRole::BodyBold:
    default: return 15;
    }
}

void hashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
}

}
std::size_t TextCache::KeyHash::operator()(const Key& key) const {
    std::size_t seed = std::hash<std::string>{}(key.text);
    hashCombine(seed, std::hash<std::uint32_t>{}(key.color));
    hashCombine(seed, std::hash<int>{}(key.wrap));
    hashCombine(seed, std::hash<int>{}(static_cast<int>(key.role)));
    return seed;
}
TextCache::~TextCache() { close(); }

bool TextCache::open(float scale, std::string& error) {
    close();
    scale_ = scale > 0.0f ? scale : 1.0f;

    for (int i = 0; i < static_cast<int>(FontRole::Count); ++i) {
        const auto role = static_cast<FontRole>(i);
        const int size = static_cast<int>(pointSize(role) * scale_ + 0.5f);
        fonts_[i] = openUiFont(std::max(8, size), error);
        if (!fonts_[i]) {
            close();
            return false;
        }
    }

    TTF_SetFontStyle(fonts_[static_cast<int>(FontRole::BodyBold)],
                     TTF_STYLE_BOLD);
    TTF_SetFontStyle(fonts_[static_cast<int>(FontRole::Title)],
                     TTF_STYLE_BOLD);

    for (TTF_Font* font : fonts_) {
        TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    }
    return true;
}

void TextCache::close() {
    for (auto& entry : entries_) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    }
    entries_.clear();

    for (TTF_Font*& font : fonts_) {
        if (font) {
            TTF_CloseFont(font);
            font = nullptr;
        }
    }
}


bool TextCache::rescale(float scale, std::string& error) {
    if (std::fabs(scale - scale_) < 0.01f) return true;
    return open(scale, error);
}

TTF_Font* TextCache::font(FontRole role) const {
    const int index = static_cast<int>(role);
    if (index < 0 || index >= static_cast<int>(FontRole::Count)) return nullptr;
    return fonts_[index];
}
int TextCache::lineHeight(FontRole role) const {
    TTF_Font* f = font(role);
    return f ? TTF_FontLineSkip(f) : 0;
}
void TextCache::measure(FontRole role, const std::string& text,
                        int* width, int* height) const {
    if (width) *width = 0;
    if (height) *height = 0;
    TTF_Font* f = font(role);
    if (!f) return;
    TTF_SizeUTF8(f, text.c_str(), width, height);
}
int TextCache::width(FontRole role, const std::string& text) const {
    int w = 0;
    measure(role, text, &w, nullptr);
    return w;
}

TextCache::Entry* TextCache::acquire(SDL_Renderer* renderer, const Key& key) {
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
        found->second.last_used = frame_;
        return &found->second;
    }
    
    TTF_Font* f = font(key.role);
    if (!f || key.text.empty()) return nullptr;

    const SDL_Color color = unpackColor(key.color);
    SDL_Surface* surface =
        key.wrap > 0
            ? TTF_RenderUTF8_Blended_Wrapped(f, key.text.c_str(), color,
                                             static_cast<Uint32>(key.wrap))
            : TTF_RenderUTF8_Blended(f, key.text.c_str(), color);
    if (!surface) return nullptr;

    Entry entry;
    entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
    entry.w = surface->w;
    entry.h = surface->h;
    entry.last_used = frame_;
    SDL_FreeSurface(surface);

    if (!entry.texture) return nullptr;
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);

    return &entries_.emplace(key, entry).first->second;
}

int TextCache::draw(SDL_Renderer* renderer, FontRole role,
                    const std::string& text, int x, int y, SDL_Color color,
                    int wrap_px) {
    Entry* entry = acquire(renderer, Key{role, packColor(color), wrap_px, text});
    if (!entry) return 0;

    SDL_Rect destination{x, y, entry->w, entry->h};
    SDL_RenderCopy(renderer, entry->texture, nullptr, &destination);
    return entry->w;
}

void TextCache::drawCentered(SDL_Renderer* renderer, FontRole role,
                             const std::string& text, const SDL_Rect& rect,
                             SDL_Color color) {
    Entry* entry = acquire(renderer, Key{role, packColor(color), 0, text});
    if (!entry) return;

    SDL_Rect destination{rect.x + (rect.w - entry->w) / 2,
                         rect.y + (rect.h - entry->h) / 2, entry->w, entry->h};
    SDL_RenderCopy(renderer, entry->texture, nullptr, &destination);
}

void TextCache::drawRight(SDL_Renderer* renderer, FontRole role,
                          const std::string& text, int right_x, int y,
                          SDL_Color color) {
    Entry* entry = acquire(renderer, Key{role, packColor(color), 0, text});
    if (!entry) return;

    SDL_Rect destination{right_x - entry->w, y, entry->w, entry->h};
    SDL_RenderCopy(renderer, entry->texture, nullptr, &destination);
}

std::string TextCache::ellipsize(FontRole role, const std::string& text,
                                 int max_px) const {
    TTF_Font* f = font(role);
    if (!f || max_px <= 0) return std::string();

    int w = 0;
    TTF_SizeUTF8(f, text.c_str(), &w, nullptr);
    if (w <= max_px) return text;

    static const char* kEllipsis = "\xE2\x80\xA6";
    std::string candidate = text;

    while (!candidate.empty()) {
        // Drop one whole code point: the trailing continuation bytes first,
        // then the lead byte they belong to. Stopping as soon as the last
        // byte is not a continuation byte leaves the lead byte dangling,
        // which makes TTF draw a replacement glyph for the tail.
        while (!candidate.empty() &&
               (static_cast<unsigned char>(candidate.back()) & 0xC0) == 0x80) {
            candidate.pop_back();
        }
        if (!candidate.empty()) candidate.pop_back();

        const std::string probe = candidate + kEllipsis;
        TTF_SizeUTF8(f, probe.c_str(), &w, nullptr);
        if (w <= max_px) return probe;
    }
    return std::string();
}


void TextCache::endFrame() {
    ++frame_;
    const bool sweep = entries_.size() > kSoftLimit || (frame_ % 300) == 0;
    if (!sweep) return;

    for (auto it = entries_.begin(); it != entries_.end();) {
        if (frame_ - it->second.last_used > kEvictAfterFrames) {
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}