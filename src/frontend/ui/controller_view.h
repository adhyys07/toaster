#pragma once

#include "painter.h"
#include "text_cache.h"
#include "theme.h"

#include <SDL.h>

#include <cstdint>
#include <string>

namespace emu::frontend::ui {

struct ControllerSnapshot {
    bool connected = false;
    std::string name;
    SDL_JoystickID instance = -1;
    SDL_JoystickPowerLevel power = SDL_JOYSTICK_POWER_UNKNOWN;

    bool buttons[SDL_CONTROLLER_BUTTON_MAX]{};
    float axes[SDL_CONTROLLER_AXIS_MAX]{};

    bool anyInput() const;
};

ControllerSnapshot readController(SDL_GameController* pad);


struct ControllerGeometry {
    SDL_Rect body{};
    SDL_Rect left_stick{};
    SDL_Rect right_stick{};
    SDL_Rect dpad{};
    SDL_Rect face{};
    SDL_Rect left_shoulder{};
    SDL_Rect right_shoulder{};
    SDL_Rect left_trigger{};
    SDL_Rect right_trigger{};
    SDL_Rect back{};
    SDL_Rect start{};
    SDL_Rect guide{};
};

ControllerGeometry layoutController(const SDL_Rect& bounds);

SDL_Rect controllerButtonRect(const ControllerGeometry& geometry,
                              SDL_GameControllerButton button);

void drawController(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                    const Theme& theme, const Metrics& metrics,
                    const SDL_Rect& bounds, const ControllerSnapshot& state,
                    SDL_GameControllerButton highlight,
                    std::uint64_t now_ms);

void drawInputDisplay(SDL_Renderer* renderer, Painter& painter,
                      TextCache& text, const Theme& theme,
                      const Metrics& metrics, const SDL_Rect& window,
                      const ControllerSnapshot& state, Uint8 opacity);

class ControllerWatcher {
public:
    void open();
    void close();

    bool handleEvent(const SDL_Event& event);

    void poll();
    const ControllerSnapshot& state() const { return state_; }
    SDL_GameController* handle() const { return pad_; }

     bool justConnected() const { return just_connected_; }
    bool justDisconnected() const { return just_disconnected_; }
    void acknowledge() { just_connected_ = just_disconnected_ = false; }

private:
    SDL_GameController* pad_ = nullptr;
    ControllerSnapshot state_;
    bool just_connected_ = false;
    bool just_disconnected_ = false;
};

}