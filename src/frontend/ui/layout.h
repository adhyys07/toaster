#pragma once

#include "theme.h"

#include <SDL.h>

namespace emu::frontend::ui {

enum class ViewMode { List, Grid };

struct LibraryLayout {
    SDL_Rect window{};
    SDL_Rect header{};
    SDL_Rect list_panel{};
    SDL_Rect detail_panel{};
    SDL_Rect search{};
    SDL_Rect chips{};
    SDL_Rect rows{};      
    SDL_Rect scrollbar{};
    SDL_Rect art{};
    SDL_Rect detail_body{};
    SDL_Rect footer{};

    int row_height = 0;
    int visible_rows = 0;

    int columns = 0;
    int cell_w = 0;
    int cell_h = 0;

    bool two_pane = true;
};

LibraryLayout layoutLibrary(int width, int height, const Metrics& metrics,
                            ViewMode mode);

SDL_Rect rowRect(const LibraryLayout& layout, int visual_row, int scroll_px);
SDL_Rect cellRect(const LibraryLayout& layout, int index, int scroll_px);

float displayScale(SDL_Window* window, SDL_Renderer* renderer);

void toRenderCoords(float scale, int* x, int* y);

}