#include "layout.h"

#include <algorithm>

namespace emu::frontend::ui
{
LibraryLayout layoutLibrary(int width, int height, const Metrics& m,
                            ViewMode mode) {
    LibraryLayout l;
    l.window = {0, 0, width, height};

    const int pad = m.pad();
    const int gap = m.gap();

    l.header = {pad, pad, std::max(0, width - 2 * pad), m.header_height()};
    l.footer = {pad, height - m.footer_height() - pad,
                std::max(0, width - 2 * pad), m.footer_height()};

    const int body_y = l.header.y + l.header.h + gap;
    const int body_h = std::max(0, l.footer.y - gap - body_y);

    l.two_pane = width >= m.px(900) && mode == ViewMode::List;

    const int detail_w =
        l.two_pane ? std::max(m.px(340), static_cast<int>(width * 0.40f)) : 0;
    const int list_w =
        std::max(0, width - 2 * pad - (l.two_pane ? detail_w + gap : 0));

    l.list_panel = {pad, body_y, list_w, body_h};
    l.detail_panel =
        l.two_pane ? SDL_Rect{pad + list_w + gap, body_y, detail_w, body_h}
                   : SDL_Rect{0, 0, 0, 0};

    const int inner_x = l.list_panel.x + pad;
    const int inner_w = std::max(0, l.list_panel.w - 2 * pad);

    l.search = {inner_x, l.list_panel.y + pad, inner_w, m.search_height()};
    l.chips = {inner_x, l.search.y + l.search.h + m.px(10), inner_w,
               m.chip_height()};

    const int rows_top = l.chips.y + l.chips.h + m.px(10);
    const int rows_h =
        std::max(0, l.list_panel.y + l.list_panel.h - pad - rows_top);
    l.rows = {inner_x, rows_top, inner_w, rows_h};

    l.scrollbar = {l.rows.x + l.rows.w - m.scrollbar_width(), l.rows.y,
                   m.scrollbar_width(), l.rows.h};

    l.row_height = m.row_height();
    l.visible_rows = l.row_height > 0 ? std::max(0, rows_h / l.row_height) : 0;

    if (mode == ViewMode::Grid) {
        // Cells are laid out in the same width rowRect uses, which stops
        // short of the scrollbar. Measuring against the full rows rect put
        // the last column under the thumb.
        const int available = std::max(0, l.rows.w - l.scrollbar.w - 4);
        const int target = m.px(150);
        l.columns = std::max(1, (available + gap) / (target + gap));
        l.cell_w = (available - gap * (l.columns - 1)) / l.columns;
        l.cell_h = static_cast<int>(l.cell_w * 4.0f / 3.0f) + m.px(34);
    }

    if (l.two_pane) {
        const int art_w = std::max(0, l.detail_panel.w - 2 * pad);
        const int art_h = std::min(static_cast<int>(art_w * 4.0f / 3.0f),
                                   static_cast<int>(l.detail_panel.h * 0.46f));
        l.art = {l.detail_panel.x + pad, l.detail_panel.y + pad, art_w, art_h};

        const int body_top = l.art.y + l.art.h + m.px(14);
        l.detail_body = {
            l.art.x, body_top, art_w,
            std::max(0, l.detail_panel.y + l.detail_panel.h - pad - body_top)};
    }

    return l;
}

SDL_Rect rowRect(const LibraryLayout& l, int visual_row, int scroll_px) {
    return {l.rows.x, l.rows.y + visual_row * l.row_height - scroll_px,
            std::max(0, l.rows.w - l.scrollbar.w - 4),
            std::max(0, l.row_height - 4)};
}

SDL_Rect cellRect(const LibraryLayout& l, int index, int scroll_px) {
    if (l.columns <= 0) return {0, 0, 0, 0};

    const int column = index % l.columns;
    const int row = index / l.columns;
    // Must match layoutLibrary's width, or the columns drift out of the pane.
    const int available = std::max(0, l.rows.w - l.scrollbar.w - 4);
    const int leftover = available - l.columns * l.cell_w;
    const int step = l.columns > 1 ? leftover / (l.columns - 1) : 0;

    return {l.rows.x + column * (l.cell_w + step),
            l.rows.y + row * (l.cell_h + step) - scroll_px, l.cell_w,
            l.cell_h};
}

float displayScale(SDL_Window* window, SDL_Renderer* renderer) {
    int logical_w = 0;
    int logical_h = 0;
    int output_w = 0;
    int output_h = 0;

    SDL_GetWindowSize(window, &logical_w, &logical_h);
    if (SDL_GetRendererOutputSize(renderer, &output_w, &output_h) != 0 ||
        logical_w <= 0) {
        return 1.0f;
    }

    float scale = static_cast<float>(output_w) / static_cast<float>(logical_w);

    if (scale < 1.05f) {
        const int index = SDL_GetWindowDisplayIndex(window);
        float ddpi = 0.0f;
        if (index >= 0 &&
            SDL_GetDisplayDPI(index, &ddpi, nullptr, nullptr) == 0 &&
            ddpi > 0.0f) {
            scale = ddpi / 96.0f;
        }
    }

    return std::min(3.0f, std::max(1.0f, scale));
}

void toRenderCoords(float scale, int*x, int*y) {
    if (x) *x = static_cast<int>(*x * scale + 0.5f);
    if (y) *y = static_cast<int>(*y * scale + 0.5f);
}

} // namespace emu::frontend::ui
