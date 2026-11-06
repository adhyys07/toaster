#pragma once

#include <SDL.h>

namespace emu::frontend::ui
{

struct Theme {
    SDL_Color bg{11, 15, 23, 255};
    SDL_Color surface{19, 26, 38, 255};
    SDL_Color surface_hover{27, 36, 52, 255};
    SDL_Color surface_active{35, 46, 66, 255};
    SDL_Color border{42, 55, 74, 255};
    SDL_Color accent{94, 200, 225, 255};
    SDL_Color accent_soft{30, 72, 90, 255};
    SDL_Color gold{244, 202, 111, 255};
    SDL_Color text{234, 240, 248, 255};
    SDL_Color text_muted{140, 156, 177, 255};
    SDL_Color text_on_accent{8, 14, 20, 255};
    SDL_Color danger{232, 110, 110, 255};
    SDL_Color success{126, 211, 150, 255};
    SDL_Color shadow{0, 0, 0, 110};
};

struct Metrics
{
    float scale = 1.0f;

    int px(int design_pixels) const {
        return static_cast<int>(design_pixels * scale + 0.5f);
    }

    int pad() const { return px(18); }
    int gap() const { return px(14); }
    // Panels and grid cells. radius_small() is for buttons, chips and rows.
    int radius() const { return px(10); }
    int radius_small() const { return px(6); }
    int border() const { return px(1) > 0 ? px(1) : 1; }
    int row_height() const { return px(44); }
    int header_height() const { return px(64); }
    int footer_height() const { return px(60); }
    int button_height() const { return px(38); }
    int chip_height() const { return px(28); }
    int search_height() const { return px(36); }
    int scrollbar_width() const { return px(6); }
};


} // namespace emu::frontend::ui
