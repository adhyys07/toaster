#pragma once

#include <SDL.h>

#include <unordered_map>

namespace emu::frontend::ui {

class Painter {
public:
    explicit Painter(SDL_Renderer* renderer);
    ~Painter();

    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    void rect(const SDL_Rect& bounds, SDL_Color color);
    void roundedRect(const SDL_Rect& bounds, int radius, SDL_Color color);

    void roundedRectBordered(const SDL_Rect& bounds, int radius,
                             SDL_Color fill, SDL_Color border, int thickness);


    void shadow(const SDL_Rect& bounds, int radius, int offset,
                SDL_Color color);

    void hline(int x, int y, int w, int thickness, SDL_Color color);
    void vline(int x, int y, int h, int thickness, SDL_Color color);

    SDL_Rect pushClip(const SDL_Rect& bounds);
    void popClip(const SDL_Rect& previous);

    void reset();

private:
    SDL_Texture* circle(int radius);

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<int, SDL_Texture*> circles_;
};

bool pointInside(const SDL_Rect& bounds, int x, int y);
SDL_Rect inset(const SDL_Rect& bounds, int by);
SDL_Color withAlpha(SDL_Color color, Uint8 alpha);
SDL_Color mix(SDL_Color a, SDL_Color b, float t);

}