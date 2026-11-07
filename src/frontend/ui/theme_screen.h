#pragma once

#include "input_map.h"
#include "painter.h"
#include "text_cache.h"
#include "theme.h"
#include "theme_io.h"
#include "widgets.h"

#include <SDL.h>

#include <string>
#include <vector>

namespace emu::frontend::ui {

class ThemeScreen {
public:
    void open(ThemeId current, const Theme& theme);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }

    const Theme& preview() const { return preview_; }
    ThemeId selectedPreset() const { return preset_; }

    bool handleEvent(const SDL_Event& event, const SDL_Rect& bounds,
                     const Metrics& metrics, float display_scale);
    bool handleAction(UiAction action);

    void draw(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Metrics& metrics, const SDL_Rect& window);

    bool wantsSave() const { return wants_save_; }
    bool wantsReset() const { return wants_reset_; }
    void acknowledge() { wants_save_ = wants_reset_ = false; }

    SDL_Rect panelRect(const SDL_Rect& window, const Metrics& m) const;

private:
    enum class Focus { Presets, Fields };
    
    void applyPreset(ThemeId id);
    void nudge(int channel, int delta);
    void refreshProblems();

    bool open_ = false;
    Theme preview_;
    Theme edited_;          
    ThemeId preset_ = ThemeId::Midnight;
    Focus focus_ = Focus::Presets;
    int field_ = 0;         
    int channel_ = 0;
    bool wants_save_ = false;
    bool wants_reset_ = false;
    std::vector<ThemeProblem> problems_;

    std::vector<Button> buttons_;
    WidgetId hot_ = kNoWidget;
};
}