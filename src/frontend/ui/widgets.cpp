#include "widgets.h"

#include <algorithm>
#include <cmath>

namespace emu::frontend::ui {

WidgetId hitTest(const std::vector<Button>& buttons, int x, int y) {
    for (const Button& button : buttons) {
        if (button.enabled && pointInside(button.rect, x, y)) return button.id;
    }
    return kNoWidget;
}

WidgetId hitTest(const std::vector<Chip>& chips, int x, int y) {
    for (const Chip& chip : chips) {
        if (pointInside(chip.rect, x, y)) return chip.id;
    }
    return kNoWidget;
}

void drawButton(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                const Theme& theme, const Metrics& metrics,
                const Button& button, WidgetId hot, WidgetId pressed,
                bool keyboard_focus) {
    const bool is_hot = button.enabled && button.id == hot;
    const bool is_pressed = button.enabled && button.id == pressed;

    SDL_Color fill = theme.surface_hover;
    SDL_Color label = theme.text;

    if (button.primary) {
        fill = is_pressed  ? mix(theme.accent, theme.bg, 0.25f)
               : is_hot    ? mix(theme.accent, theme.text, 0.12f)
                           : theme.accent;
        label = theme.text_on_accent;
    } else if (is_pressed) {
        fill = theme.surface_active;
    } else if (is_hot) {
        fill = mix(theme.surface_hover, theme.accent_soft, 0.45f);
    }

    if (!button.enabled) {
        fill = theme.surface;
        label = theme.text_muted;
    }

    const SDL_Color border =
        keyboard_focus ? theme.accent
                       : (is_hot ? mix(theme.border, theme.accent, 0.5f)
                                 : theme.border);

    painter.roundedRectBordered(
        button.rect, metrics.radius_small(), fill, border,
        keyboard_focus ? metrics.px(2) : metrics.border());
    text.drawCentered(renderer, FontRole::Body, button.label, button.rect,
                      label);
}

void drawChip(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics, const Chip& chip,
              WidgetId hot) {
    const bool is_hot = chip.id == hot;

    const SDL_Color fill = chip.selected
                               ? theme.accent_soft
                               : (is_hot ? theme.surface_hover : theme.surface);
    const SDL_Color border = chip.selected ? theme.accent : theme.border;

    painter.roundedRectBordered(chip.rect, chip.rect.h / 2, fill, border,
                                metrics.border());
    text.drawCentered(renderer, FontRole::Small, chip.label, chip.rect,
                      chip.selected ? theme.text : theme.text_muted);
}

void drawSearchField(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                     const Theme& theme, const Metrics& metrics,
                     const SDL_Rect& rect, const std::string& value,
                     bool active, bool caret_visible) {
    painter.roundedRectBordered(rect, metrics.radius_small(),
                                active ? theme.surface_active : theme.surface,
                                active ? theme.accent : theme.border,
                                active ? metrics.px(2) : metrics.border());

    const int x = rect.x + metrics.px(12);
    const int text_y = rect.y + (rect.h - text.lineHeight(FontRole::Body)) / 2;

    if (value.empty() && !active) {
        text.draw(renderer, FontRole::Body, "Search  /", x, text_y,
                  theme.text_muted);
        return;
    }

    const int available = rect.w - metrics.px(24);
    const int drawn =
        text.draw(renderer, FontRole::Body,
                  text.ellipsize(FontRole::Body, value, available), x, text_y,
                  theme.text);

    if (active && caret_visible) {
        painter.rect({x + drawn + metrics.px(2), text_y,
                      std::max(1, metrics.px(2)),
                      text.lineHeight(FontRole::Body)},
                     theme.accent);
    }
}

void drawScrollbar(SDL_Renderer* renderer, Painter& painter,
                   const Theme& theme, const Metrics& metrics,
                   const SDL_Rect& track, int content_px, int viewport_px,
                   int scroll_px) {
    (void)renderer;
    if (content_px <= viewport_px || viewport_px <= 0) return;

    painter.roundedRect(track, track.w / 2, withAlpha(theme.border, 90));

    const float visible =
        static_cast<float>(viewport_px) / static_cast<float>(content_px);
    const int thumb_h =
        std::max(metrics.px(28), static_cast<int>(track.h * visible));
    const int travel = track.h - thumb_h;
    const int max_scroll = content_px - viewport_px;
    const int offset =
        max_scroll > 0 ? static_cast<int>(
                             travel * (static_cast<float>(scroll_px) /
                                       static_cast<float>(max_scroll)))
                       : 0;

    painter.roundedRect({track.x, track.y + offset, track.w, thumb_h},
                        track.w / 2, theme.border);
}

void drawPanel(SDL_Renderer* renderer, Painter& painter, const Theme& theme,
               const Metrics& metrics, const SDL_Rect& rect) {
    (void)renderer;
    painter.shadow(rect, metrics.radius(), metrics.px(3), theme.shadow);
    painter.roundedRectBordered(rect, metrics.radius(), theme.surface,
                                theme.border, metrics.border());
}

bool Smoothed::step(float dt_seconds, float rate) {
    const float delta = target - current;
    if (std::fabs(delta) < 0.5f) {
        const bool moved = current != target;
        current = target;
        return moved;
    }
    current += delta * (1.0f - std::exp(-rate * dt_seconds));
    return true;
}

void ToastStack::push(std::string message, Toast::Kind kind,
                      std::uint64_t now_ms) {
    Toast toast;
    toast.message = std::move(message);
    toast.kind = kind;
    toast.shown_at_ms = now_ms;
    toast.lifetime_ms = kind == Toast::Kind::Error ? 6000 : 3400;

    toasts_.push_back(std::move(toast));
    while (toasts_.size() > kMaxVisible) toasts_.pop_front();
}

bool ToastStack::active(std::uint64_t now_ms) const {
    for (const Toast& toast : toasts_) {
        if (now_ms - toast.shown_at_ms < toast.lifetime_ms) return true;
    }
    return false;
}

void ToastStack::draw(SDL_Renderer* renderer, Painter& painter,
                      TextCache& text, const Theme& theme,
                      const Metrics& metrics, const SDL_Rect& window,
                      std::uint64_t now_ms) {
    while (!toasts_.empty() &&
           now_ms - toasts_.front().shown_at_ms >=
               toasts_.front().lifetime_ms) {
        toasts_.pop_front();
    }

    int y = window.y + window.h - metrics.pad();

    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const Toast& toast = *it;
        const std::uint64_t age = now_ms - toast.shown_at_ms;

        const std::uint32_t fade_at =
            toast.lifetime_ms > 400 ? toast.lifetime_ms - 400 : 0;
        float alpha = 1.0f;
        if (age > fade_at) {
            alpha = 1.0f - static_cast<float>(age - fade_at) / 400.0f;
        }
        alpha = std::min(1.0f, std::max(0.0f, alpha));

        const int width = std::min(
            window.w - 2 * metrics.pad(),
            text.width(FontRole::Body, toast.message) + metrics.px(56));
        const int height = metrics.px(44);
        y -= height + metrics.px(8);

        const SDL_Rect rect{window.x + window.w - metrics.pad() - width, y,
                            width, height};

        SDL_Color tint = theme.accent;
        if (toast.kind == Toast::Kind::Error) tint = theme.danger;
        if (toast.kind == Toast::Kind::Success) tint = theme.success;

        painter.shadow(
            rect, metrics.radius_small(), metrics.px(3),
            withAlpha(theme.shadow,
                      static_cast<Uint8>(theme.shadow.a * alpha)));
        painter.roundedRectBordered(
            rect, metrics.radius_small(),
            withAlpha(theme.surface_hover, static_cast<Uint8>(245 * alpha)),
            withAlpha(tint, static_cast<Uint8>(255 * alpha)),
            metrics.border());

        painter.roundedRect({rect.x + metrics.px(10),
                             rect.y + rect.h / 2 - metrics.px(4),
                             metrics.px(8), metrics.px(8)},
                            metrics.px(4),
                            withAlpha(tint, static_cast<Uint8>(255 * alpha)));

        text.draw(renderer, FontRole::Body,
                  text.ellipsize(FontRole::Body, toast.message,
                                 rect.w - metrics.px(48)),
                  rect.x + metrics.px(28),
                  rect.y + (rect.h - text.lineHeight(FontRole::Body)) / 2,
                  withAlpha(theme.text, static_cast<Uint8>(255 * alpha)));
    }
}

void drawProgressBar(SDL_Renderer* renderer, Painter& painter,
                     const Theme& theme, const Metrics& metrics,
                     const SDL_Rect& rect, float fraction) {
    (void)renderer;
    (void)metrics;
    fraction = std::min(1.0f, std::max(0.0f, fraction));

    painter.roundedRect(rect, rect.h / 2, theme.surface_active);
    const int filled = static_cast<int>(rect.w * fraction);
    if (filled > 0) {
        painter.roundedRect({rect.x, rect.y, std::max(rect.h, filled), rect.h},
                            rect.h / 2, theme.accent);
    }
}

} // namespace emu::frontend::ui
