#pragma once

#include "theme.h"

#include <SDL.h>

namespace emu::frontend::ui {

struct BigPictureLayout {
    SDL_Rect window{};
    SDL_Rect hero{};      
    SDL_Rect title{};
    SDL_Rect meta{};
    SDL_Rect strip{};   
    SDL_Rect hints{};
    int cell_w = 0;
    int cell_h = 0;
    int visible_cells = 0;
};

constexpr float kBigPictureScale = 1.8f;

Metrics bigPictureMetrics(const Metrics& base);

BigPictureLayout layoutBigPicture(const SDL_Rect& window, const Metrics& m);

int carouselIndex(int selected, int count, int offset);


SDL_Rect carouselCellRect(const BigPictureLayout& layout, int offset);

} // namespace emu::frontend::ui
