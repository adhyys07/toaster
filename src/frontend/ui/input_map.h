#pragma once

#include <SDL.h>

#include <cstdint>

namespace emu::frontend::ui {

enum class UiAction {
    None,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End,
    Confirm,
    Cancel,
    ToggleFavorite,
    ToggleView,
    Search,
    NextTab,
    PreviousTab,
    Menu,
    Refresh
};

class InputMap {
public:
    void open();
    void close();

    UiAction translate(const SDL_Event* event);
    UiAction repeat(std::uint32_t dt_ms);

private:
    SDL_GameController* controller_ = nullptr;
    UiAction held_ = UiAction::None;
    std::uint32_t held_for_ms_ = 0;
    bool axis_latched_[4]{};

    static constexpr std::uint32_t kFirstRepeatMs = 380;
    static constexpr std::uint32_t kRepeatMs = 60;
};

}