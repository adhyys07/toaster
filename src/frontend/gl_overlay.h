#pragma once

#include <SDL.h>

#include <string>

namespace emu::frontend {

class GlOverlay {
public:
    GlOverlay() = default;
    ~GlOverlay();

    GlOverlay(const GlOverlay&) = delete;
    GlOverlay& operator=(const GlOverlay&) = delete;

    bool resize(SDL_Window* window, std::string& error);
    SDL_Renderer* renderer() const { return renderer_; }

    int width() const { return width_; }
    int height() const { return height_; }
    
    float scale() const { return scale_; }

    void clear();

    void markDrawn() { drawn_ = true; }
    bool drawn() const { return drawn_; }

    void composite();

    void shutdown();

private:
    SDL_Window* window_ = nullptr;
    SDL_Surface* surface_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    unsigned texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    float scale_ = 1.0f;
    bool drawn_ = false;
};

}