#include "gallery.h"

#include "../artwork_cache.h"
#include "../screenshot.h"
#include "layout.h"
#include "widgets.h"

#include <algorithm>
#include <cstdio>

namespace emu::frontend::ui {
namespace {

constexpr int kThumbColumns = 4;

std::string shortName(const std::filesystem::path& path) {
    return path.filename().string();
}

} // namespace

void GalleryScreen::open(const std::string& game_path) {
    game_path_ = game_path;
    selected_ = 0;
    scroll_row_ = 0;
    full_screen_view_ = false;
    reload();
    open_ = true;
}

void GalleryScreen::reload() {
    shots_ = game_path_.empty()
        ? emu::frontend::listAllScreenshots()
        : emu::frontend::listScreenshots(game_path_);
    if (selected_ >= static_cast<int>(shots_.size())) {
        selected_ = static_cast<int>(shots_.size()) - 1;
    }
    if (selected_ < 0) selected_ = 0;
}

bool GalleryScreen::consumeDeleted(std::string& path) {
    if (deleted_.empty()) return false;
    path = deleted_;
    deleted_.clear();
    return true;
}

void GalleryScreen::deleteSelected() {
    if (shots_.empty()) return;
    const std::filesystem::path target =
        shots_[static_cast<std::size_t>(selected_)];

    std::error_code ec;
    std::filesystem::remove(target, ec);
    if (ec) return;

    deleted_ = shortName(target);
    reload();
}

int GalleryScreen::columns(const SDL_Rect& body, const Metrics& m) const {
    (void)body;
    (void)m;
    return kThumbColumns;
}

SDL_Rect GalleryScreen::panelRect(const SDL_Rect& window,
                                  const Metrics& m) const {
    if (full_screen_view_) return window;
    const int width = std::min(m.px(860), window.w - 2 * m.pad());
    const int height = std::min(m.px(620), window.h - 2 * m.pad());
    return SDL_Rect{window.x + (window.w - width) / 2,
                    window.y + (window.h - height) / 2, width, height};
}

SDL_Rect GalleryScreen::bodyRect(const SDL_Rect& panel,
                                 const Metrics& m) const {
    const int top = panel.y + m.pad() + m.px(26);
    const int bottom = panel.y + panel.h - m.pad() - m.px(18);
    return SDL_Rect{panel.x + m.pad(), top, panel.w - 2 * m.pad(),
                    std::max(m.px(60), bottom - top)};
}

SDL_Rect GalleryScreen::cellRectAt(const SDL_Rect& body, const Metrics& m,
                                   int index) const {
    const int cols = columns(body, m);
    const int gap = m.gap();
    const int cell_w = (body.w - (cols - 1) * gap) / cols;
    const int cell_h = cell_w * 3 / 4 + m.px(20);
    const int column = index % cols;
    const int row = index / cols - scroll_row_;
    return SDL_Rect{body.x + column * (cell_w + gap),
                    body.y + row * (cell_h + gap), cell_w, cell_h};
}

bool GalleryScreen::handleAction(UiAction action) {
    if (!open_) return false;
    const int count = static_cast<int>(shots_.size());

    switch (action) {
    case UiAction::Left:
        if (count) selected_ = std::max(0, selected_ - 1);
        return true;
    case UiAction::Right:
        if (count) selected_ = std::min(count - 1, selected_ + 1);
        return true;
    case UiAction::Up:
        if (count) selected_ = std::max(0, selected_ - kThumbColumns);
        return true;
    case UiAction::Down:
        if (count) selected_ = std::min(count - 1, selected_ + kThumbColumns);
        return true;
    case UiAction::Confirm:
        if (count) full_screen_view_ = !full_screen_view_;
        return true;
    case UiAction::Cancel:
        if (full_screen_view_) full_screen_view_ = false;
        else close();
        return true;
    default:
        return false;
    }
}

bool GalleryScreen::handleEvent(const SDL_Event& event,
                                const SDL_Rect& window, const Metrics& metrics,
                                float display_scale) {
    if (!open_) return false;
    (void)window;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_LEFT:   return handleAction(UiAction::Left);
        case SDL_SCANCODE_RIGHT:  return handleAction(UiAction::Right);
        case SDL_SCANCODE_UP:     return handleAction(UiAction::Up);
        case SDL_SCANCODE_DOWN:   return handleAction(UiAction::Down);
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!event.key.repeat) handleAction(UiAction::Confirm);
            return true;
        case SDL_SCANCODE_ESCAPE:
            return handleAction(UiAction::Cancel);
        case SDL_SCANCODE_DELETE:
            deleteSelected();
            return true;
        case SDL_SCANCODE_F5:
            reload();
            return true;
        default:
            return true;
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        int x = event.motion.x;
        int y = event.motion.y;
        toRenderCoords(display_scale, &x, &y);
        hot_ = -1;
        for (int i = 0; i < static_cast<int>(shots_.size()); ++i) {
            if (pointInside(cellRectAt(last_body_, metrics, i), x, y)) {
                hot_ = i;
                break;
            }
        }
        return true;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        int x = event.button.x;
        int y = event.button.y;
        toRenderCoords(display_scale, &x, &y);
        if (full_screen_view_) {
            full_screen_view_ = false;
            return true;
        }
        if (!pointInside(last_panel_, x, y)) {
            close();
            return true;
        }
        if (hot_ >= 0) {
            selected_ = hot_;
            full_screen_view_ = true;
        }
        return true;
    }

    if (event.type == SDL_MOUSEWHEEL) {
        scroll_row_ = std::max(0, scroll_row_ - event.wheel.y);
        return true;
    }

    return false;
}

void GalleryScreen::draw(SDL_Renderer* renderer, Painter& painter,
                         TextCache& text, const Theme& theme,
                         const Metrics& m, const SDL_Rect& window,
                         emu::frontend::ArtworkCache& art) {
    if (!open_) return;

    painter.rect(window, withAlpha(theme.bg, full_screen_view_ ? 240 : 205));
    const SDL_Rect panel = panelRect(window, m);
    last_panel_ = panel;

    if (full_screen_view_ && !shots_.empty()) {
        SDL_Texture* texture = art.get(
            shots_[static_cast<std::size_t>(selected_)].string());
        if (texture) {
            int tw = 0;
            int th = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
            const float sx = static_cast<float>(panel.w) /
                             static_cast<float>(std::max(1, tw));
            const float sy = static_cast<float>(panel.h) /
                             static_cast<float>(std::max(1, th));
            const float fit = std::min(sx, sy);
            const int w = static_cast<int>(tw * fit);
            const int h = static_cast<int>(th * fit);
            const SDL_Rect dst{panel.x + (panel.w - w) / 2,
                               panel.y + (panel.h - h) / 2, w, h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
        }
        text.draw(renderer, FontRole::Small,
                  shortName(shots_[static_cast<std::size_t>(selected_)]) +
                      "   Esc back   Del delete",
                  panel.x + m.pad(), panel.y + panel.h - m.pad() - m.px(14),
                  theme.text_muted);
        return;
    }

    painter.shadow(panel, m.radius(), m.px(6), theme.shadow);
    painter.roundedRectBordered(panel, m.radius(), theme.surface, theme.border,
                                m.border());

    const std::string title =
        "Screenshots (" + std::to_string(shots_.size()) + ")";
    text.draw(renderer, FontRole::BodyBold, title, panel.x + m.pad(),
              panel.y + m.pad(), theme.text);

    const SDL_Rect body = bodyRect(panel, m);
    last_body_ = body;

    if (shots_.empty()) {
        text.drawCentered(renderer, FontRole::Body,
                          "Nothing here yet - press the screenshot hotkey "
                          "while a game is running.",
                          body, theme.text_muted);
        return;
    }

    const SDL_Rect previous = painter.pushClip(body);
    for (int i = 0; i < static_cast<int>(shots_.size()); ++i) {
        const SDL_Rect cell = cellRectAt(body, m, i);
        if (cell.y + cell.h < body.y || cell.y > body.y + body.h) continue;

        const bool selected = i == selected_;
        painter.roundedRectBordered(
            cell, m.radius_small(),
            selected ? theme.surface_active
                     : (i == hot_ ? theme.surface_hover : theme.bg),
            selected ? theme.accent : theme.border, m.border());

        const SDL_Rect image = inset(cell, m.border());
        const SDL_Rect thumb{image.x, image.y, image.w,
                             image.h - m.px(18)};
        if (SDL_Texture* texture = art.get(shots_[static_cast<std::size_t>(i)]
                                               .string())) {
            // Letterbox rather than stretch: a 4:3 NES frame squeezed into
            // the cell is a worse preview than one with bars, and the shots
            // in one folder are not all the same shape.
            int tw = 0;
            int th = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
            const float fit = std::min(
                static_cast<float>(thumb.w) / static_cast<float>(std::max(1, tw)),
                static_cast<float>(thumb.h) / static_cast<float>(std::max(1, th)));
            const int fw = std::max(1, static_cast<int>(tw * fit));
            const int fh = std::max(1, static_cast<int>(th * fit));
            const SDL_Rect fitted{thumb.x + (thumb.w - fw) / 2,
                                  thumb.y + (thumb.h - fh) / 2, fw, fh};
            SDL_RenderCopy(renderer, texture, nullptr, &fitted);
        } else {
            text.drawCentered(renderer, FontRole::Small, "...", thumb,
                              theme.text_muted);
        }

        text.draw(renderer, FontRole::Small,
                  text.ellipsize(FontRole::Small,
                                 shortName(shots_[static_cast<std::size_t>(i)]),
                                 cell.w - m.px(10)),
                  cell.x + m.px(5), thumb.y + thumb.h + m.px(2),
                  theme.text_muted);
    }
    painter.popClip(previous);

    text.draw(renderer, FontRole::Small,
              "Enter view   Del delete   F5 refresh   Esc close",
              panel.x + m.pad(), panel.y + panel.h - m.pad() - m.px(14),
              theme.text_muted);
}

} // namespace emu::frontend::ui
