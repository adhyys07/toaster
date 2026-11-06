#include "painter.h"

#include <algorithm>
#include <cmath>

namespace emu::frontend::ui {

Painter::Painter(SDL_Renderer* renderer) : renderer_(renderer) {}

Painter::~Painter() { reset(); }

void Painter::reset() {
    for (auto& entry : circles_) {
        if (entry.second) SDL_DestroyTexture(entry.second);
    }
    circles_.clear();
}

SDL_Texture* Painter::circle(int radius) {
    const auto found = circles_.find(radius);
    if (found != circles_.end()) return found->second;

    const int size = radius * 2;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        circles_[radius] = nullptr;
        return nullptr;
    }

    auto* pixels = static_cast<Uint32*>(surface->pixels);
    const int stride = surface->pitch / 4;
    const double centre = radius;
    constexpr int kSamples = 4;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int covered = 0;
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const double dx = x + (sx + 0.5) / kSamples - centre;
                    const double dy = y + (sy + 0.5) / kSamples - centre;
                    if (dx * dx + dy * dy <= centre * centre) ++covered;
                }
            }
            const auto alpha =
                static_cast<Uint8>(255 * covered / (kSamples * kSamples));
            pixels[y * stride + x] =
                SDL_MapRGBA(surface->format, 255, 255, 255, alpha);
        }
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    circles_[radius] = texture;
    return texture;
}

void Painter::rect(const SDL_Rect& bounds, SDL_Color color) {
    if (bounds.w <= 0 || bounds.h <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer_, &bounds);
}

void Painter::roundedRect(const SDL_Rect& bounds, int radius,
                          SDL_Color color) {
    if (bounds.w <= 0 || bounds.h <= 0) return;

    radius = std::max(0, std::min(radius, std::min(bounds.w, bounds.h) / 2));
    if (radius == 0) {
        rect(bounds, color);
        return;
    }

    SDL_Texture* disc = circle(radius);
    if (!disc) {
        rect(bounds, color);
        return;
    }

    SDL_SetTextureColorMod(disc, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(disc, color.a);

    const int r = radius;
    const SDL_Rect source[4] = {
        {0, 0, r, r}, {r, 0, r, r}, {0, r, r, r}, {r, r, r, r}};
    const SDL_Rect destination[4] = {
        {bounds.x, bounds.y, r, r},
        {bounds.x + bounds.w - r, bounds.y, r, r},
        {bounds.x, bounds.y + bounds.h - r, r, r},
        {bounds.x + bounds.w - r, bounds.y + bounds.h - r, r, r}};

    for (int i = 0; i < 4; ++i) {
        SDL_RenderCopy(renderer_, disc, &source[i], &destination[i]);
    }

    rect({bounds.x + r, bounds.y, bounds.w - 2 * r, r}, color);
    rect({bounds.x + r, bounds.y + bounds.h - r, bounds.w - 2 * r, r}, color);
    rect({bounds.x, bounds.y + r, bounds.w, bounds.h - 2 * r}, color);
}

void Painter::roundedRectBordered(const SDL_Rect& bounds, int radius,
                                  SDL_Color fill, SDL_Color border,
                                  int thickness) {
    thickness = std::max(1, thickness);
    roundedRect(bounds, radius, border);
    roundedRect(inset(bounds, thickness), std::max(0, radius - thickness),
                fill);
}

void Painter::shadow(const SDL_Rect& bounds, int radius, int offset,
                     SDL_Color color) {
    for (int step = 3; step >= 1; --step) {
        const SDL_Rect spread{bounds.x - step, bounds.y + offset - step / 2,
                              bounds.w + step * 2, bounds.h + step * 2};
        roundedRect(spread, radius + step,
                    withAlpha(color, static_cast<Uint8>(color.a / (step * 2))));
    }
}

void Painter::hline(int x, int y, int w, int thickness, SDL_Color color) {
    rect({x, y, w, std::max(1, thickness)}, color);
}

void Painter::vline(int x, int y, int h, int thickness, SDL_Color color) {
    rect({x, y, std::max(1, thickness), h}, color);
}

SDL_Rect Painter::pushClip(const SDL_Rect& bounds) {
    SDL_Rect previous{0, 0, 0, 0};
    if (SDL_RenderIsClipEnabled(renderer_)) {
        SDL_RenderGetClipRect(renderer_, &previous);
    }
    SDL_RenderSetClipRect(renderer_, &bounds);
    return previous;
}

void Painter::popClip(const SDL_Rect& previous) {
    if (previous.w > 0 && previous.h > 0) {
        SDL_RenderSetClipRect(renderer_, &previous);
    } else {
        SDL_RenderSetClipRect(renderer_, nullptr);
    }
}

bool pointInside(const SDL_Rect& bounds, int x, int y) {
    return x >= bounds.x && x < bounds.x + bounds.w && y >= bounds.y &&
           y < bounds.y + bounds.h;
}

SDL_Rect inset(const SDL_Rect& bounds, int by) {
    return {bounds.x + by, bounds.y + by, std::max(0, bounds.w - 2 * by),
            std::max(0, bounds.h - 2 * by)};
}

SDL_Color withAlpha(SDL_Color color, Uint8 alpha) {
    color.a = alpha;
    return color;
}

SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    const auto lerp = [t](Uint8 from, Uint8 to) {
        return static_cast<Uint8>(from + (to - from) * t + 0.5f);
    };
    return SDL_Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b),
                     lerp(a.a, b.a)};
}

} // namespace emu::frontend::ui
