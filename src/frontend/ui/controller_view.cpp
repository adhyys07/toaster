#include "controller_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace emu::frontend::ui {
namespace {

constexpr float kAspect = 0.62f;    
// The widget is three bands stacked vertically: the triggers, the body, and
// a caption. Sizing the body to the whole rect pushed the triggers out of it
// and over whatever was behind.
constexpr float kTriggerBand = 0.105f;   // trigger height plus its gap
constexpr float kCaptionBand = 0.150f;   // name and battery under the pad
constexpr float kTotalHeight = kTriggerBand + kAspect + kCaptionBand;

constexpr float kStickRadius = 0.088f;
// The d-pad reads as an afterthought when it is much smaller than the face
// cluster, so both are sized from the same budget.
constexpr float kDpadArm = 0.066f;
constexpr float kDpadThickness = 0.056f;

struct Normalized { float x, y; };

// Positions are fractions of the body: x of its width, y of its height.
// The clusters are spaced so none of them touch; testControllerLayout
// asserts that, because the first two attempts at these numbers overlapped
// in ways that only showed up on screen.
constexpr Normalized kLeftStick{0.215f, 0.400f};
// The right stick clears the face buttons vertically rather than
// horizontally: the body is not wide enough to separate four clusters
// across it at a readable size.
constexpr Normalized kRightStick{0.625f, 0.780f};
constexpr Normalized kDpad{0.410f, 0.715f};
constexpr Normalized kFace{0.780f, 0.400f};
constexpr Normalized kBack{0.430f, 0.360f};
constexpr Normalized kStart{0.570f, 0.360f};
constexpr Normalized kGuide{0.500f, 0.490f};

SDL_Point at(const SDL_Rect& body, Normalized position) {
    return {body.x + static_cast<int>(body.w * position.x),
            body.y + static_cast<int>(body.h * position.y)};
}

SDL_Rect centred(SDL_Point centre, int radius) {
    return {centre.x - radius, centre.y - radius, radius * 2, radius * 2};
}

int scaled(const SDL_Rect& body, float fraction) {
    return std::max(1, static_cast<int>(body.w * fraction));
}

void circle(Painter& painter, SDL_Point centre, int radius, SDL_Color color) {
    painter.roundedRect(centred(centre, radius), radius, color);
}

void circleBordered(Painter& painter, SDL_Point centre, int radius,
                    SDL_Color fill, SDL_Color border, int thickness) {
    painter.roundedRectBordered(centred(centre, radius), radius, fill, border,
                                thickness);
}

SDL_Color controlColor(bool pressed, bool highlighted, const Theme& theme,
                       std::uint64_t now_ms) {
    if (pressed) return theme.accent;
    if (highlighted) {
        const float phase =
            0.5f + 0.5f * std::sin(static_cast<float>(now_ms) / 180.0f);
        return mix(theme.surface_active, theme.gold, phase);
    }
    return theme.surface_active;
}

const char* faceLabel(SDL_GameControllerButton button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
    default: return "";
    }
}

} // namespace

bool ControllerSnapshot::anyInput() const {
    for (bool pressed : buttons) {
        if (pressed) return true;
    }
    for (float axis : axes) {
        if (std::fabs(axis) > 0.2f) return true;
    }
    return false;
}

ControllerSnapshot readController(SDL_GameController* pad) {
    ControllerSnapshot snapshot;
    if (!pad || !SDL_GameControllerGetAttached(pad)) return snapshot;

    snapshot.connected = true;
    if (const char* name = SDL_GameControllerName(pad)) snapshot.name = name;

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(pad);
    if (joystick) {
        snapshot.instance = SDL_JoystickInstanceID(joystick);
        snapshot.power = SDL_JoystickCurrentPowerLevel(joystick);
    }

    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        snapshot.buttons[i] = SDL_GameControllerGetButton(
                                  pad, static_cast<SDL_GameControllerButton>(i))
                              != 0;
    }

    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        const auto axis = static_cast<SDL_GameControllerAxis>(i);
        const float raw = SDL_GameControllerGetAxis(pad, axis) / 32767.0f;

        snapshot.axes[i] =
            (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
             axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                ? std::clamp(raw, 0.0f, 1.0f)
                : std::clamp(raw, -1.0f, 1.0f);
    }

    return snapshot;
}

ControllerGeometry layoutController(const SDL_Rect& bounds) {
    ControllerGeometry g;
    if (bounds.w <= 0 || bounds.h <= 0) return g;

    // Fit the whole widget, triggers and caption included, so nothing is
    // drawn outside the rect the caller gave us.
    int width = bounds.w;
    if (static_cast<int>(width * kTotalHeight) > bounds.h) {
        width = static_cast<int>(bounds.h / kTotalHeight);
    }
    const int total = static_cast<int>(width * kTotalHeight);
    const int height = static_cast<int>(width * kAspect);

    const int left = bounds.x + (bounds.w - width) / 2;
    const int top = bounds.y + (bounds.h - total) / 2;

    g.body = {left, top + static_cast<int>(width * kTriggerBand), width,
              height};

    const int stick_r = scaled(g.body, kStickRadius);
    g.left_stick = centred(at(g.body, kLeftStick), stick_r);
    g.right_stick = centred(at(g.body, kRightStick), stick_r);

    const int dpad_half = scaled(g.body, kDpadArm + kDpadThickness / 2);
    g.dpad = centred(at(g.body, kDpad), dpad_half);

    // Sized so the diagonal gap between two face buttons clears their radii:
    // controllerButtonRect spaces them at face.w/3 with radius face.w/5, and
    // (1/3)*sqrt(2) > 2*(1/5).
    const int face_half = scaled(g.body, 0.135f);
    g.face = centred(at(g.body, kFace), face_half);

    const int small_r = scaled(g.body, 0.022f);
    g.back = centred(at(g.body, kBack), small_r);
    g.start = centred(at(g.body, kStart), small_r);
    g.guide = centred(at(g.body, kGuide), scaled(g.body, 0.030f));

    // Heights come off the body's height, not its width. Mixing the two put
    // the shoulders a third of the way down the pad and straight through the
    // face buttons.
    const int shoulder_w = scaled(g.body, 0.185f);
    const int shoulder_h = std::max(1, static_cast<int>(g.body.h * 0.085f));
    g.left_shoulder = {g.body.x + scaled(g.body, 0.105f),
                       g.body.y + static_cast<int>(g.body.h * 0.045f),
                       shoulder_w, shoulder_h};
    g.right_shoulder = {g.body.x + g.body.w - scaled(g.body, 0.105f) -
                            shoulder_w,
                        g.left_shoulder.y, shoulder_w, shoulder_h};

    // Above the shoulders, the way they sit on the hardware, but inside the
    // band reserved for them at the top of the widget.
    const int trigger_h = scaled(g.body, 0.075f);
    const int trigger_y = g.body.y - static_cast<int>(
                                         g.body.w * kTriggerBand) +
                          scaled(g.body, 0.012f);
    g.left_trigger = {g.left_shoulder.x, trigger_y, shoulder_w, trigger_h};
    g.right_trigger = {g.right_shoulder.x, trigger_y, shoulder_w, trigger_h};

    return g;
}

SDL_Rect controllerButtonRect(const ControllerGeometry& g,
                              SDL_GameControllerButton button) {
    // r = w/5 with a w/3 offset: diagonal neighbours clear each other, which
    // r = w/4 did not, so X and Y overlapped into a blob.
    const int face_r = std::max(1, g.face.w / 5);
    const SDL_Point face_centre{g.face.x + g.face.w / 2,
                                g.face.y + g.face.h / 2};
    const int offset = g.face.w / 3;

    const int arm = std::max(1, g.dpad.w / 3);
    const SDL_Point dpad_centre{g.dpad.x + g.dpad.w / 2,
                                g.dpad.y + g.dpad.h / 2};

    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        return centred({face_centre.x, face_centre.y + offset}, face_r);
    case SDL_CONTROLLER_BUTTON_B:
        return centred({face_centre.x + offset, face_centre.y}, face_r);
    case SDL_CONTROLLER_BUTTON_X:
        return centred({face_centre.x - offset, face_centre.y}, face_r);
    case SDL_CONTROLLER_BUTTON_Y:
        return centred({face_centre.x, face_centre.y - offset}, face_r);

    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return {dpad_centre.x - arm / 2, dpad_centre.y - arm * 3 / 2, arm, arm};
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return {dpad_centre.x - arm / 2, dpad_centre.y + arm / 2, arm, arm};
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return {dpad_centre.x - arm * 3 / 2, dpad_centre.y - arm / 2, arm, arm};
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return {dpad_centre.x + arm / 2, dpad_centre.y - arm / 2, arm, arm};

    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return g.left_shoulder;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return g.right_shoulder;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return g.left_stick;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return g.right_stick;
    case SDL_CONTROLLER_BUTTON_BACK:          return g.back;
    case SDL_CONTROLLER_BUTTON_START:         return g.start;
    case SDL_CONTROLLER_BUTTON_GUIDE:         return g.guide;

    default:
        return {0, 0, 0, 0};
    }
}

void drawController(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                    const Theme& theme, const Metrics& m,
                    const SDL_Rect& bounds, const ControllerSnapshot& state,
                    SDL_GameControllerButton highlight,
                    std::uint64_t now_ms) {
    const ControllerGeometry g = layoutController(bounds);
    if (g.body.w <= 0 || g.body.h <= 0) return;

    const auto pressed = [&](SDL_GameControllerButton button) {
        return button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX &&
               state.buttons[button];
    };
    const auto colorFor = [&](SDL_GameControllerButton button) {
        return controlColor(pressed(button), button == highlight, theme,
                            now_ms);
    };

    const SDL_Color shell_fill =
        state.connected ? theme.surface : theme.bg;

    // The grips go down first and unbordered. Outlining each piece drew the
    // seams between them, so the pad read as a rectangle with two circles
    // stuck on rather than one silhouette.
    // Wider than they are protruding, and only softly rounded: at
    // radius = width/2 they rendered as two circles stuck under a box.
    const int grip_w = g.body.w / 4;
    const int grip_h = g.body.h * 3 / 5;
    const SDL_Rect left_grip{g.body.x + g.body.w / 12,
                             g.body.y + g.body.h - grip_h, grip_w, grip_h};
    const SDL_Rect right_grip{g.body.x + g.body.w - g.body.w / 12 - grip_w,
                              left_grip.y, grip_w, grip_h};
    painter.roundedRect(left_grip, grip_w / 3, shell_fill);
    painter.roundedRect(right_grip, grip_w / 3, shell_fill);

    const SDL_Rect shell{g.body.x, g.body.y, g.body.w, g.body.h * 7 / 10};
    painter.roundedRectBordered(shell, shell.h / 3, shell_fill, theme.border,
                                m.border());


    const auto drawTrigger = [&](const SDL_Rect& rect, float value,
                                 const char* label) {
        painter.roundedRectBordered(rect, rect.h / 3, theme.surface_active,
                                    theme.border, m.border());
        const int filled = static_cast<int>(rect.h * std::clamp(value, 0.0f, 1.0f));
        if (filled > 0) {
            painter.roundedRect({rect.x, rect.y + rect.h - filled, rect.w,
                                 filled},
                                rect.h / 3, theme.accent);
        }
        text.drawCentered(renderer, FontRole::Small, label, rect,
                          value > 0.5f ? theme.text_on_accent
                                       : theme.text_muted);
    };

    drawTrigger(g.left_trigger,
                state.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT], "LT");
    drawTrigger(g.right_trigger,
                state.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT], "RT");

    painter.roundedRect(g.left_shoulder, g.left_shoulder.h / 2,
                        colorFor(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    text.drawCentered(renderer, FontRole::Small, "LB", g.left_shoulder,
                      pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                          ? theme.text_on_accent
                          : theme.text_muted);

    painter.roundedRect(g.right_shoulder, g.right_shoulder.h / 2,
                        colorFor(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    text.drawCentered(renderer, FontRole::Small, "RB", g.right_shoulder,
                      pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                          ? theme.text_on_accent
                          : theme.text_muted);

    for (const SDL_GameControllerButton button :
         {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
          SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT}) {
        painter.roundedRect(controllerButtonRect(g, button), m.px(2),
                            colorFor(button));
    }


    for (const SDL_GameControllerButton button :
         {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
          SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y}) {
        const SDL_Rect rect = controllerButtonRect(g, button);
        painter.roundedRect(rect, rect.w / 2, colorFor(button));
        text.drawCentered(renderer, FontRole::Small, faceLabel(button), rect,
                          pressed(button) ? theme.text_on_accent
                                          : theme.text_muted);
    }

    const auto drawStick = [&](const SDL_Rect& well, float x_axis,
                               float y_axis,
                               SDL_GameControllerButton click) {
        const SDL_Point centre{well.x + well.w / 2, well.y + well.h / 2};
        const int radius = well.w / 2;

        circleBordered(painter, centre, radius, theme.bg, theme.border,
                       m.border());

        const int travel = radius / 2;
        const SDL_Point cap{
            centre.x + static_cast<int>(x_axis * travel),
            centre.y + static_cast<int>(y_axis * travel)};
        circle(painter, cap, radius * 3 / 5, colorFor(click));

        painter.rect({centre.x - m.px(1), centre.y - m.px(1), m.px(2),
                      m.px(2)},
                     theme.text_muted);
    };

    drawStick(g.left_stick, state.axes[SDL_CONTROLLER_AXIS_LEFTX],
              state.axes[SDL_CONTROLLER_AXIS_LEFTY],
              SDL_CONTROLLER_BUTTON_LEFTSTICK);
    drawStick(g.right_stick, state.axes[SDL_CONTROLLER_AXIS_RIGHTX],
              state.axes[SDL_CONTROLLER_AXIS_RIGHTY],
              SDL_CONTROLLER_BUTTON_RIGHTSTICK);


    painter.roundedRect(g.back, g.back.w / 2,
                        colorFor(SDL_CONTROLLER_BUTTON_BACK));
    painter.roundedRect(g.start, g.start.w / 2,
                        colorFor(SDL_CONTROLLER_BUTTON_START));
    painter.roundedRect(g.guide, g.guide.w / 2,
                        colorFor(SDL_CONTROLLER_BUTTON_GUIDE));


    const int caption_y = g.body.y + g.body.h + m.px(10);
    if (!state.connected) {
        text.drawCentered(renderer, FontRole::Body, "No controller connected",
                          {g.body.x, caption_y, g.body.w, m.px(22)},
                          theme.text_muted);
        return;
    }

    text.drawCentered(
        renderer, FontRole::Body,
        text.ellipsize(FontRole::Body, state.name, g.body.w),
        {g.body.x, caption_y, g.body.w, m.px(22)}, theme.text);

    const char* battery = nullptr;
    switch (state.power) {
    case SDL_JOYSTICK_POWER_EMPTY:  battery = "battery empty"; break;
    case SDL_JOYSTICK_POWER_LOW:    battery = "battery low"; break;
    case SDL_JOYSTICK_POWER_MEDIUM: battery = "battery medium"; break;
    case SDL_JOYSTICK_POWER_FULL:   battery = "battery full"; break;
    case SDL_JOYSTICK_POWER_WIRED:  battery = "wired"; break;
    default: break;
    }
    if (battery) {
        text.drawCentered(
            renderer, FontRole::Small, battery,
            {g.body.x, caption_y + m.px(22), g.body.w, m.px(18)},
            state.power == SDL_JOYSTICK_POWER_LOW ||
                    state.power == SDL_JOYSTICK_POWER_EMPTY
                ? theme.danger
                : theme.text_muted);
    }
}

void drawInputDisplay(SDL_Renderer* renderer, Painter& painter,
                      TextCache& text, const Theme& theme, const Metrics& m,
                      const SDL_Rect& window, const ControllerSnapshot& state,
                      Uint8 opacity) {
    if (!state.connected || opacity == 0) return;

    const int width = m.px(220);
    const int height = static_cast<int>(width * kAspect) + m.px(30);
    const SDL_Rect box{window.x + window.w - width - m.pad(),
                       window.y + window.h - height - m.pad(), width, height};

    painter.roundedRect(box, m.radius(),
                        withAlpha(theme.bg,
                                  static_cast<Uint8>(opacity * 0.72f)));
    drawController(renderer, painter, text, theme, m,
                   inset(box, m.px(8)), state,
                   SDL_CONTROLLER_BUTTON_INVALID, 0);
}


void ControllerWatcher::open() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0 &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "Controller support unavailable: %s\n",
                     SDL_GetError());
        return;
    }
    SDL_GameControllerEventState(SDL_ENABLE);

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        pad_ = SDL_GameControllerOpen(i);
        if (pad_) {
            just_connected_ = true;
            break;
        }
    }
    poll();
}

void ControllerWatcher::close() {
    if (pad_) {
        SDL_GameControllerClose(pad_);
        pad_ = nullptr;
    }
    state_ = ControllerSnapshot{};
}

bool ControllerWatcher::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (!pad_) {
            pad_ = SDL_GameControllerOpen(event.cdevice.which);
            if (pad_) {
                just_connected_ = true;
                poll();
                return true;
            }
        }
        return false;

    case SDL_CONTROLLERDEVICEREMOVED:
        if (pad_ && event.cdevice.which == state_.instance) {
            SDL_GameControllerClose(pad_);
            pad_ = nullptr;
            state_ = ControllerSnapshot{};
            just_disconnected_ = true;

            for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                if (!SDL_IsGameController(i)) continue;
                pad_ = SDL_GameControllerOpen(i);
                if (pad_) {
                    just_connected_ = true;
                    poll();
                    break;
                }
            }
            return true;
        }
        return false;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
        poll();
        return true;

    default:
        return false;
    }
}

void ControllerWatcher::poll() { state_ = readController(pad_); }

} // namespace emu::frontend::ui
