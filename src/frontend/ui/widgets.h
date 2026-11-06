#pragma once

#include "painter.h"
#include "text_cache.h"
#include "theme.h"

#include <SDL.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace emu::frontend::ui {

using WidgetId = std::uint32_t;
constexpr WidgetId kNoWidget = 0;

struct Button {
    WidgetId id = kNoWidget;
    SDL_Rect rect{};
    std::string label;
    bool primary = false;
    bool enabled = true;
};

struct Chip {
    WidgetId id = kNoWidget;
    SDL_Rect rect{};
    std::string label;
    bool selected = false;
};

WidgetId hitTest(const std::vector<Button>& buttons, int x, int y);
WidgetId hitTest(const std::vector<Chip>& chips, int x, int y);

void drawButton(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                const Theme& theme, const Metrics& metrics,
                const Button& button, WidgetId hot, WidgetId pressed,
                bool keyboard_focus);

void drawChip(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics, const Chip& chip,
              WidgetId hot);

void drawSearchField(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                     const Theme& theme, const Metrics& metrics,
                     const SDL_Rect& rect, const std::string& value,
                     bool active, bool caret_visible);

void drawScrollbar(SDL_Renderer* renderer, Painter& painter,
                   const Theme& theme, const Metrics& metrics,
                   const SDL_Rect& track, int content_px, int viewport_px,
                   int scroll_px);

void drawPanel(SDL_Renderer* renderer, Painter& painter, const Theme& theme,
               const Metrics& metrics, const SDL_Rect& rect);


struct Smoothed {
    float current = 0.0f;
    float target = 0.0f;

    void snap(float value) { current = target = value; }
    bool animating() const { return current != target; }
    bool step(float dt_seconds, float rate = 18.0f);
};


struct Toast {
    enum class Kind { Info, Success, Error };

    std::string message;
    Kind kind = Kind::Info;
    std::uint64_t shown_at_ms = 0;
    std::uint32_t lifetime_ms = 3400;
};

class ToastStack {
public:
    void push(std::string message, Toast::Kind kind, std::uint64_t now_ms);
    bool active(std::uint64_t now_ms) const;
    void draw(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics,
              const SDL_Rect& window, std::uint64_t now_ms);

private:
    std::deque<Toast> toasts_;
    static constexpr std::size_t kMaxVisible = 3;
};

void drawProgressBar(SDL_Renderer* renderer, Painter& painter,
                     const Theme& theme, const Metrics& metrics,
                     const SDL_Rect& rect, float fraction);

} // namespace emu::frontend::ui
