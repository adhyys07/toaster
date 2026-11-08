#include "big_picture.h"

#include <algorithm>

namespace emu::frontend::ui {

Metrics bigPictureMetrics(const Metrics& base) {
    Metrics scaled = base;
    scaled.scale = base.scale * kBigPictureScale;
    return scaled;
}

BigPictureLayout layoutBigPicture(const SDL_Rect& window, const Metrics& m) {
    BigPictureLayout layout;
    layout.window = window;

    const int pad = m.pad();

    layout.cell_h = std::min(m.px(150), std::max(m.px(60), window.h / 4));
    layout.cell_w = (layout.cell_h * 3) / 4;   // roughly boxart proportions

    const int strip_h = layout.cell_h + pad;
    layout.strip = SDL_Rect{window.x, window.y + window.h - strip_h - pad,
                            window.w, strip_h};

    layout.hints = SDL_Rect{window.x + pad,
                            layout.strip.y - m.row_height(),
                            window.w - 2 * pad, m.row_height()};

    const int top = window.y + pad;
    const int available = layout.hints.y - top;

    const int text_h = m.row_height() * 2;
    const int hero_h = std::max(m.px(80), available - text_h - pad);
    const int hero_w = (hero_h * 3) / 4;

    layout.hero = SDL_Rect{window.x + (window.w - hero_w) / 2, top, hero_w,
                           hero_h};
    layout.title = SDL_Rect{window.x + pad, layout.hero.y + hero_h,
                            window.w - 2 * pad, m.row_height()};
    layout.meta = SDL_Rect{window.x + pad, layout.title.y + m.row_height(),
                           window.w - 2 * pad, m.row_height()};

    const int step = layout.cell_w + m.gap();
    if (step > 0) {
        int fits = window.w / step;
        if (fits % 2 == 0) --fits;
        layout.visible_cells = std::max(1, fits);
    } else {
        layout.visible_cells = 1;
    }

    return layout;
}

int carouselIndex(int selected, int count, int offset) {
    if (count <= 0) return -1;
    const int index = selected + offset;
    if (index < 0 || index >= count) return -1;
    return index;
}

SDL_Rect carouselCellRect(const BigPictureLayout& layout, int offset) {
    const int centre_x = layout.window.x + layout.window.w / 2;
    const int step = layout.cell_w + (layout.cell_w / 8);
    return SDL_Rect{centre_x - layout.cell_w / 2 + offset * step,
                    layout.strip.y, layout.cell_w, layout.cell_h};
}

} // namespace emu::frontend::ui
