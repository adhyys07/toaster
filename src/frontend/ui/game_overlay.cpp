#include "game_overlay.h"

// toRenderCoords: mouse coordinates arrive in window units and the panel is
// laid out in render units, which differ on a HiDPI display.
#include "layout.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace emu::frontend::ui {
namespace {

enum : int {
    kRowNone = 0,

    kPauseResume = 100,
    kPauseStates,
    kPauseReset,
    kPauseScreenshot,
    kPauseGallery,
    kPauseFullscreen,
    kPauseQuit,

    kVideoUpscaler = 200,
    kVideoScaling,
    kVideoSharpness,
    kVideoVsync,
    kVideoFps,
    kVideoInputDisplay,
    kVideoRewind,
    kVideoRunAhead,
    kVideoFocusPause,

    kAudioMute = 300,
    kAudioVolume,
    kAudioBuffer,
    kAudioRumble,

    kPadFirst = 400,
    kKeysPadFirst = 500, 
    kKeysHotkeyFirst = 600,  
    kTurboFirst = 650,
    kCheatFirst = 700,

    // The per-game page edits a fixed set of overrides, so these are
    // named rather than indexed: an id here means one specific field.
    kOptionFirst = 800,
    kOptionScale = kOptionFirst,
    kOptionInteger,
    kOptionBilinear,
    kOptionVsync,
    kOptionFps,
    kOptionMute,
    kOptionFastForward
};

const char* kPadActionNames[emu::frontend::kPadActionCount] = {
    "Cross", "Circle", "Square", "Triangle",
    "Start", "Select",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "L1", "R1"
};

SDL_GameControllerButton diagramButton(int pad_action) {
    switch (pad_action) {
    case emu::frontend::PadCross:    return SDL_CONTROLLER_BUTTON_A;
    case emu::frontend::PadCircle:   return SDL_CONTROLLER_BUTTON_B;
    case emu::frontend::PadSquare:   return SDL_CONTROLLER_BUTTON_X;
    case emu::frontend::PadTriangle: return SDL_CONTROLLER_BUTTON_Y;
    case emu::frontend::PadStart:    return SDL_CONTROLLER_BUTTON_START;
    case emu::frontend::PadSelect:   return SDL_CONTROLLER_BUTTON_BACK;
    case emu::frontend::PadUp:       return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case emu::frontend::PadDown:     return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case emu::frontend::PadLeft:     return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case emu::frontend::PadRight:    return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    case emu::frontend::PadL1:
        return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case emu::frontend::PadR1:
        return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    default: return SDL_CONTROLLER_BUTTON_INVALID;
    }
}

std::string padButtonLabel(int button) {
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) {
        return "Button " + std::to_string(button);
    }
    const char* name = SDL_GameControllerGetStringForButton(
        static_cast<SDL_GameControllerButton>(button));
    return (name && *name) ? name : "Button " + std::to_string(button);
}

std::string onOff(bool value) { return value ? "On" : "Off"; }

std::string percent(int value) { return std::to_string(value) + "%"; }

}

const char* overlayPageName(OverlayPage page) {
    switch (page) {
    case OverlayPage::Pause:       return "Pause";
    case OverlayPage::States:      return "States";
    case OverlayPage::Video:       return "Video";
    case OverlayPage::Audio:       return "Audio";
    case OverlayPage::Pad:         return "Controller";
    case OverlayPage::Keys:        return "Keyboard";
    case OverlayPage::Cheats:      return "Cheats";
    case OverlayPage::GameOptions: return "This game";
    default:                       return "";
    }
}

void GameOverlay::setHooks(OverlayHooks hooks) {
    hooks_ = std::move(hooks);
    dirty_ = true;
}

void GameOverlay::open(OverlayPage page) {
    open_ = true;
    page_ = page;
    selected_ = 0;
    scroll_ = 0;
    capture_ = -1;
    dirty_ = true;
}

void GameOverlay::close() {
    open_ = false;
    capture_ = -1;
}

void GameOverlay::cancelCapture() { capture_ = -1; }

const GameOverlay::Row* GameOverlay::selectedRow() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(selected_)];
}

void GameOverlay::rebuild() {
    dirty_ = false;
    rows_.clear();

    const emu::frontend::Settings* s = hooks_.settings;

    switch (page_) {
    case OverlayPage::Pause:
        rows_.push_back({"Resume", "", kPauseResume, false, true});
        rows_.push_back({"Save states", "", kPauseStates, false,
                         static_cast<bool>(hooks_.slotView)});
        rows_.push_back({"Reset the game", "", kPauseReset, false,
                         static_cast<bool>(hooks_.reset)});
        rows_.push_back({"Screenshot", "", kPauseScreenshot, false,
                         static_cast<bool>(hooks_.screenshot)});
        rows_.push_back({"Screenshot gallery", "", kPauseGallery, false,
                         static_cast<bool>(hooks_.openGallery)});
        rows_.push_back({"Fullscreen", "", kPauseFullscreen, false,
                         static_cast<bool>(hooks_.toggleFullscreen)});
        rows_.push_back({"Quit to library", "", kPauseQuit, false, true});
        break;
    
    case OverlayPage::Video:
        if (!s) break;
        rows_.push_back({"Upscaler",
                         emu::frontend::upscalerName(s->upscaler),
                         kVideoUpscaler, false, true});
        rows_.push_back({"Scaling",
                         emu::frontend::scalingName(s->scaling),
                         kVideoScaling, false, true});
        {
            char sharpness[16] = {};
            std::snprintf(sharpness, sizeof(sharpness), "%.2f",
                          static_cast<double>(s->sharpness));
            rows_.push_back({"Sharpness", sharpness, kVideoSharpness, false,
                             true});
        }
        rows_.push_back({"VSync", onOff(s->vsync), kVideoVsync, false, true});
        rows_.push_back({"FPS counter", onOff(s->show_fps), kVideoFps, false,
                         true});
        rows_.push_back({"Input display", onOff(s->input_display),
                         kVideoInputDisplay, false, true});
        rows_.push_back({"Rewind history",
                         s->rewind_seconds > 0
                             ? std::to_string(s->rewind_seconds) + " s"
                             : std::string("Off"),
                         kVideoRewind, false, true});
        // Counted in frames of latency removed, which is one less than the
        // frames simulated: a setting of 1 simulates one frame and removes
        // none, so it reads as Off.
        rows_.push_back({"Run-ahead",
                         s->run_ahead_frames > 1
                             ? std::to_string(s->run_ahead_frames - 1) +
                                   " frames (restart)"
                             : std::string("Off"),
                         kVideoRunAhead, false, true});
        rows_.push_back({"Pause when unfocused",
                         onOff(s->pause_on_focus_loss), kVideoFocusPause,
                         false, true});
        break;

    case OverlayPage::Audio:
        if (!s) break;
        rows_.push_back({"Mute", onOff(s->mute), kAudioMute, false, true});
        rows_.push_back({"Volume", percent(s->volume), kAudioVolume, false,
                         !s->mute});
        rows_.push_back({"Buffer",
                         std::to_string(s->audio_buffer_samples) +
                             " samples (restart)",
                         kAudioBuffer, false, true});
        rows_.push_back({"Controller rumble", onOff(s->rumble), kAudioRumble,
                         false, true});
        break;

    case OverlayPage::Pad:
        if (!s) break;
        rows_.push_back({"Press Enter on a row, then a pad button", "",
                         kRowNone, true, false});
        for (int i = 0; i < emu::frontend::kPadActionCount; ++i) {
            rows_.push_back({kPadActionNames[i],
                             padButtonLabel(s->pad_buttons[i]),
                             kPadFirst + i, false, true});
        }
        rows_.push_back({"Turbo", "", kRowNone, true, false});
        // The mask is over PadAction indices rather than a core's button
        // bits, because the bit order differs per system while this list does
        // not.
        for (int i = 0; i < emu::frontend::kPadActionCount; ++i) {
            rows_.push_back({kPadActionNames[i],
                             onOff((s->turbo_mask & (1u << i)) != 0),
                             kTurboFirst + i, false, true});
        }
        break;
    
    case OverlayPage::Keys:
        if (!s || !hooks_.hotkeys) break;
        rows_.push_back({"Game buttons", "", kRowNone, true, false});
        for (int i = 0; i < emu::frontend::kPadActionCount; ++i) {
            const char* name = SDL_GetScancodeName(
                static_cast<SDL_Scancode>(s->keyboard_keys[i]));
            rows_.push_back({kPadActionNames[i],
                             (name && *name) ? name : "Not bound",
                             kKeysPadFirst + i, false, true});
        }
        rows_.push_back({"Hotkeys", "", kRowNone, true, false});
        for (int i = 0; i < emu::frontend::kHotkeyCount; ++i) {
            const auto action = emu::frontend::kHotkeys[i].action;
            rows_.push_back({emu::frontend::kHotkeys[i].label,
                             emu::frontend::describeHotkey(
                                 hooks_.hotkeys->binding(action)),
                             kKeysHotkeyFirst + i, false, true});
        }
        break;
    
    case OverlayPage::Cheats:
        if (!hooks_.cheats || hooks_.cheats->empty()) {
            rows_.push_back({"No cheats loaded for this game", "", kRowNone,
                             true, false});
            break;
        }
        for (std::size_t i = 0; i < hooks_.cheats->size(); ++i) {
            const emu::frontend::Cheat& cheat = (*hooks_.cheats)[i];
            char address[16] = {};
            std::snprintf(address, sizeof(address), "%04X",
                          static_cast<unsigned>(cheat.address));
            rows_.push_back({cheat.description.empty()
                                 ? std::string("Patch ") + address
                                 : cheat.description,
                             onOff(cheat.enabled),
                             kCheatFirst + static_cast<int>(i), false, true});
        }
        break;
    

    case OverlayPage::GameOptions: {
        const emu::frontend::GameSettings* g = hooks_.game_settings;
        if (!g) {
            rows_.push_back({"This game has no saved options", "", kRowNone,
                             true, false});
            break;
        }
        rows_.push_back({"Overrides apply to this game only", "", kRowNone,
                         true, false});
        // The "present" flags are what distinguish "this game is set to 2x"
        // from "this game inherits 2x", and the player has to see which, or
        // clearing an override looks like it did nothing.
        rows_.push_back({"Window scale",
                         std::to_string(g->scale) + "x" +
                             (g->present.scale ? "" : " (inherited)"),
                         kOptionScale, false, true});
        rows_.push_back({"Integer scaling",
                         onOff(g->integer_scale) +
                             (g->present.integer_scale ? "" : " (inherited)"),
                         kOptionInteger, false, true});
        rows_.push_back({"Bilinear filter",
                         onOff(g->bilinear_filter) +
                             (g->present.bilinear_filter ? ""
                                                         : " (inherited)"),
                         kOptionBilinear, false, true});
        rows_.push_back({"VSync",
                         onOff(g->vsync) +
                             (g->present.vsync ? "" : " (inherited)"),
                         kOptionVsync, false, true});
        rows_.push_back({"FPS counter",
                         onOff(g->show_fps) +
                             (g->present.show_fps ? "" : " (inherited)"),
                         kOptionFps, false, true});
        rows_.push_back({"Mute",
                         onOff(g->mute_audio) +
                             (g->present.mute_audio ? "" : " (inherited)"),
                         kOptionMute, false, true});
        rows_.push_back({"Fast forward",
                         std::to_string(g->fast_forward_speed) + "x" +
                             (g->present.fast_forward_speed
                                  ? ""
                                  : " (inherited)"),
                         kOptionFastForward, false, true});
        break;
    }

    default:
        break;
    }

    // A rebuild can shorten the list under the cursor, and an out-of-range
    // selection would read past the end on the next draw.
    if (selected_ >= static_cast<int>(rows_.size())) {
        selected_ = static_cast<int>(rows_.size()) - 1;
    }
    if (selected_ < 0) selected_ = 0;

    // Headers are not selectable, so a page whose first row is one has to
    // step past it or the first keypress appears to do nothing. Bounded by
    // the row count so a page made entirely of headers cannot spin here.
    const int count = static_cast<int>(rows_.size());
    for (int step = 0; step < count; ++step) {
        const Row& row = rows_[static_cast<std::size_t>(selected_)];
        if (!row.header && row.enabled) break;
        selected_ = (selected_ + 1) % count;
    }
}

// ---------------------------------------------------------------------------
// Geometry
//
// The free functions are the real implementations and the members forward to
// them, so the headless tests and the drawing code cannot drift apart.
// ---------------------------------------------------------------------------

SDL_Rect overlayPanelRect(const SDL_Rect& window, const Metrics& m) {
    // Capped in design pixels rather than taken as a fraction of the window,
    // so the panel does not stretch into a letterbox on an ultrawide display.
    const int width = std::min(m.px(620), window.w - 2 * m.pad());
    const int height = std::min(m.px(560), window.h - 2 * m.pad());
    return SDL_Rect{window.x + (window.w - width) / 2,
                    window.y + (window.h - height) / 2, width, height};
}

SDL_Rect overlayTabsRect(const SDL_Rect& panel, const Metrics& m) {
    return SDL_Rect{panel.x + m.pad(), panel.y + m.pad(),
                    panel.w - 2 * m.pad(), m.chip_height()};
}

SDL_Rect overlayBodyRect(const SDL_Rect& panel, const Metrics& m) {
    const SDL_Rect tabs = overlayTabsRect(panel, m);
    const int top = tabs.y + tabs.h + m.gap();
    return SDL_Rect{panel.x + m.pad(), top, panel.w - 2 * m.pad(),
                    panel.y + panel.h - m.pad() - top};
}

SDL_Rect overlaySlotRect(const SDL_Rect& body, const Metrics& m, int slot) {
    // Ten slots in a five-wide grid. Cell width comes from the body width and
    // cell height from a design constant: taking both from the width made the
    // cells grow taller on a wide window, which is exactly the mistake the
    // header comment warns about.
    constexpr int kColumns = 5;
    const int gap = m.gap();
    const int cell_w = (body.w - gap * (kColumns - 1)) / kColumns;
    const int cell_h = m.px(104);
    const int column = slot % kColumns;
    const int row = slot / kColumns;
    return SDL_Rect{body.x + column * (cell_w + gap),
                    body.y + row * (cell_h + gap), cell_w, cell_h};
}

SDL_Rect GameOverlay::panelRect(const SDL_Rect& window,
                                const Metrics& m) const {
    return overlayPanelRect(window, m);
}

SDL_Rect GameOverlay::tabsRect(const SDL_Rect& panel, const Metrics& m) const {
    return overlayTabsRect(panel, m);
}

SDL_Rect GameOverlay::bodyRect(const SDL_Rect& panel, const Metrics& m) const {
    return overlayBodyRect(panel, m);
}

SDL_Rect GameOverlay::rowRectAt(const SDL_Rect& body, const Metrics& m,
                                int index) const {
    return SDL_Rect{body.x, body.y + index * m.row_height() - scroll_, body.w,
                    m.row_height()};
}

SDL_Rect GameOverlay::slotRectAt(const SDL_Rect& body, const Metrics& m,
                                 int slot) const {
    return overlaySlotRect(body, m, slot);
}

int GameOverlay::visibleRows(const SDL_Rect& body, const Metrics& m) const {
    const int height = m.row_height();
    return height > 0 ? std::max(1, body.h / height) : 1;
}

void GameOverlay::clampScroll(const SDL_Rect& body, const Metrics& m) {
    const int content = static_cast<int>(rows_.size()) * m.row_height();
    const int limit = std::max(0, content - body.h);

    // Keeping the cursor on screen lives here rather than in move(), because
    // a window resize changes what fits without the selection moving at all.
    const int top = selected_ * m.row_height();
    const int bottom = top + m.row_height();
    if (top < scroll_) {
        scroll_ = top;
    } else if (bottom > scroll_ + body.h) {
        scroll_ = bottom - body.h;
    }
    scroll_ = std::clamp(scroll_, 0, limit);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void GameOverlay::move(int delta) {
    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());

    int index = selected_;
    for (int step = 0; step < count; ++step) {
        index += delta;
        if (index < 0) index = count - 1;
        if (index >= count) index = 0;
        const Row& row = rows_[static_cast<std::size_t>(index)];
        if (!row.header && row.enabled) {
            selected_ = index;
            return;
        }
    }
}

void GameOverlay::moveVertical(int delta) {
    if (page_ == OverlayPage::States) {
        // The grid is five wide, so up and down move by a whole row and stop
        // at the edges rather than wrapping into the opposite corner.
        const int next = slot_ + delta * 5;
        if (next >= 0 && next < 10) slot_ = next;
        return;
    }
    move(delta);
}

void GameOverlay::moveHorizontal(int delta, bool from_pad) {
    if (page_ == OverlayPage::States) {
        const int next = slot_ + delta;
        if (next >= 0 && next < 10) slot_ = next;
        return;
    }

    // Sideways on the Controller page would edit a binding, so a stick
    // leaving centre would rebind whatever the cursor happened to be on. The
    // pad changes page there instead; the keyboard still adjusts.
    if (from_pad && page_ == OverlayPage::Pad) {
        switchPage(delta);
        return;
    }

    adjust(delta);
}

void GameOverlay::switchPage(int delta) {
    const int count = static_cast<int>(OverlayPage::Count);
    int index = static_cast<int>(page_);
    index = ((index + delta) % count + count) % count;
    page_ = static_cast<OverlayPage>(index);
    selected_ = 0;
    scroll_ = 0;
    // An armed binding row does not survive leaving its page, or the next
    // keypress elsewhere would be swallowed as a binding.
    capture_ = -1;
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// Editing
//
// adjust() changes a value in place (Left/Right); activate() is Enter, which
// toggles a flag, runs an action, or arms a binding row for capture.
// ---------------------------------------------------------------------------

void GameOverlay::adjust(int delta) {
    const Row* row = selectedRow();
    if (!row || row->header || !row->enabled) return;
    const int id = row->id;

    emu::frontend::Settings* s = hooks_.settings;
    emu::frontend::GameSettings* g = hooks_.game_settings;

    bool changed = true;

    if (s && id == kVideoUpscaler) {
        const int count = emu::frontend::kUpscalerCount;
        const int next =
            ((static_cast<int>(s->upscaler) + delta) % count + count) % count;
        s->upscaler = static_cast<emu::frontend::Upscaler>(next);
    } else if (s && id == kVideoScaling) {
        const int count = 3;
        const int next =
            ((static_cast<int>(s->scaling) + delta) % count + count) % count;
        s->scaling = static_cast<emu::frontend::ScalingMode>(next);
    } else if (s && id == kVideoSharpness) {
        s->sharpness =
            std::clamp(s->sharpness + static_cast<float>(delta) * 0.05f, 0.0f,
                       1.0f);
    } else if (s && id == kVideoVsync) {
        s->vsync = !s->vsync;
    } else if (s && id == kVideoFps) {
        s->show_fps = !s->show_fps;
    } else if (s && id == kVideoInputDisplay) {
        s->input_display = !s->input_display;
    } else if (s && id == kVideoRewind) {
        s->rewind_seconds = std::clamp(s->rewind_seconds + delta * 5, 0, 120);
    } else if (s && id == kVideoRunAhead) {
        s->run_ahead_frames = std::clamp(s->run_ahead_frames + delta, 1, 8);
    } else if (s && id == kVideoFocusPause) {
        s->pause_on_focus_loss = !s->pause_on_focus_loss;
    } else if (s && id == kAudioMute) {
        s->mute = !s->mute;
    } else if (s && id == kAudioVolume) {
        s->volume = std::clamp(s->volume + delta * 5, 0, 100);
    } else if (s && id == kAudioBuffer) {
        // Doubling rather than stepping: the buffer is a power of two and the
        // useful range spans four octaves, which a linear step would crawl.
        int samples = s->audio_buffer_samples;
        samples = delta > 0 ? samples * 2 : samples / 2;
        s->audio_buffer_samples = std::clamp(samples, 256, 4096);
    } else if (s && id == kAudioRumble) {
        s->rumble = !s->rumble;
    } else if (s && id >= kTurboFirst &&
               id < kTurboFirst + emu::frontend::kPadActionCount) {
        s->turbo_mask ^= 1u << (id - kTurboFirst);
    } else if (hooks_.cheats && id >= kCheatFirst &&
               id < kCheatFirst +
                        static_cast<int>(hooks_.cheats->size())) {
        emu::frontend::Cheat& cheat =
            (*hooks_.cheats)[static_cast<std::size_t>(id - kCheatFirst)];
        cheat.enabled = !cheat.enabled;
        if (hooks_.cheatsChanged) hooks_.cheatsChanged();
        dirty_ = true;
        return;
    } else if (g && id == kOptionScale) {
        g->scale = std::clamp(g->scale + delta, 1, 8);
        g->present.scale = true;
    } else if (g && id == kOptionInteger) {
        g->integer_scale = !g->integer_scale;
        g->present.integer_scale = true;
    } else if (g && id == kOptionBilinear) {
        g->bilinear_filter = !g->bilinear_filter;
        g->present.bilinear_filter = true;
    } else if (g && id == kOptionVsync) {
        g->vsync = !g->vsync;
        g->present.vsync = true;
    } else if (g && id == kOptionFps) {
        g->show_fps = !g->show_fps;
        g->present.show_fps = true;
    } else if (g && id == kOptionMute) {
        g->mute_audio = !g->mute_audio;
        g->present.mute_audio = true;
    } else if (g && id == kOptionFastForward) {
        g->fast_forward_speed =
            std::clamp(g->fast_forward_speed + delta, 1, 16);
        g->present.fast_forward_speed = true;
    } else {
        changed = false;
    }

    if (!changed) return;

    // Editing a row on the per-game page writes that file, not the global
    // one, or the override would be lost on the next launch.
    if (id >= kOptionFirst) {
        if (hooks_.persistGameOptions) hooks_.persistGameOptions();
    } else {
        if (hooks_.applySettings) hooks_.applySettings();
        if (hooks_.persistSettings) hooks_.persistSettings();
    }
    dirty_ = true;
}

void GameOverlay::activate() {
    const Row* row = selectedRow();
    if (!row || row->header || !row->enabled) return;
    const int id = row->id;

    switch (id) {
    case kPauseResume:
        close();
        if (hooks_.resume) hooks_.resume();
        return;
    case kPauseStates:
        page_ = OverlayPage::States;
        selected_ = 0;
        scroll_ = 0;
        dirty_ = true;
        return;
    case kPauseReset:
        if (hooks_.reset) hooks_.reset();
        close();
        return;
    case kPauseScreenshot:
        if (hooks_.screenshot) hooks_.screenshot();
        return;
    case kPauseGallery:
        if (hooks_.openGallery) hooks_.openGallery();
        return;
    case kPauseFullscreen:
        if (hooks_.toggleFullscreen) hooks_.toggleFullscreen();
        return;
    case kPauseQuit:
        if (hooks_.quit) hooks_.quit();
        return;
    default:
        break;
    }

    // A binding row arms itself and waits for the next key or pad button.
    // Stored as the row id rather than the row index so a rebuild between
    // arming and pressing cannot retarget it.
    if ((id >= kPadFirst && id < kPadFirst + emu::frontend::kPadActionCount) ||
        (id >= kKeysPadFirst &&
         id < kKeysPadFirst + emu::frontend::kPadActionCount) ||
        (id >= kKeysHotkeyFirst &&
         id < kKeysHotkeyFirst + emu::frontend::kHotkeyCount)) {
        capture_ = id;
        dirty_ = true;
        return;
    }

    // Everything else is a value: Enter means the same as Right, which is
    // what makes the on/off rows feel like checkboxes.
    adjust(1);
}

bool GameOverlay::captureButton(SDL_GameControllerButton button) {
    if (capture_ < 0 || !hooks_.settings) return false;

    // Only the pad page binds pad buttons. A keyboard row stays armed, so
    // brushing the stick does not silently bind the wrong device.
    if (capture_ < kPadFirst ||
        capture_ >= kPadFirst + emu::frontend::kPadActionCount) {
        return false;
    }

    const int index = capture_ - kPadFirst;
    hooks_.settings->pad_buttons[static_cast<std::size_t>(index)] =
        static_cast<int>(button);
    capture_ = -1;
    if (hooks_.persistSettings) hooks_.persistSettings();
    dirty_ = true;
    return true;
}

bool GameOverlay::handleAction(UiAction action) {
    if (!open_) return false;

    switch (action) {
    case UiAction::Up:
        moveVertical(-1);
        return true;
    case UiAction::Down:
        moveVertical(1);
        return true;
    case UiAction::Left:
        moveHorizontal(-1, true);
        return true;
    case UiAction::Right:
        moveHorizontal(1, true);
        return true;
    case UiAction::PageUp:
        if (page_ == OverlayPage::States) {
            slot_ = std::max(0, slot_ - 5);
        } else {
            for (int i = 0; i < 5; ++i) move(-1);
        }
        return true;
    case UiAction::PageDown:
        if (page_ == OverlayPage::States) {
            slot_ = std::min(9, slot_ + 5);
        } else {
            for (int i = 0; i < 5; ++i) move(1);
        }
        return true;
    case UiAction::Home:
        if (page_ == OverlayPage::States) {
            slot_ = 0;
        } else {
            selected_ = 0;
            move(1);
            move(-1);
        }
        return true;
    case UiAction::End:
        if (page_ == OverlayPage::States) {
            slot_ = 9;
        } else if (!rows_.empty()) {
            selected_ = static_cast<int>(rows_.size()) - 1;
            move(-1);
            move(1);
        }
        return true;
    case UiAction::Confirm:
        if (page_ == OverlayPage::States) {
            if (hooks_.saveSlot) hooks_.saveSlot(slot_);
            dirty_ = true;
        } else {
            activate();
        }
        return true;
    case UiAction::Cancel:
        // An armed binding row cancels first: otherwise there is no way to
        // back out of a rebind without binding something.
        if (capture_ >= 0) {
            cancelCapture();
            dirty_ = true;
            return true;
        }
        close();
        if (hooks_.resume) hooks_.resume();
        return true;
    case UiAction::NextTab:
        switchPage(1);
        return true;
    case UiAction::PreviousTab:
        switchPage(-1);
        return true;
    case UiAction::Menu:
        close();
        if (hooks_.resume) hooks_.resume();
        return true;
    default:
        return false;
    }
}

bool GameOverlay::handleEvent(const SDL_Event& event, const SDL_Rect& window,
                              const Metrics& metrics, float display_scale) {
    if (!open_) return false;
    if (dirty_) rebuild();

    const SDL_Rect panel = panelRect(window, metrics);
    const SDL_Rect body = bodyRect(panel, metrics);
    last_panel_ = panel;
    last_body_ = body;

    // An armed binding row consumes the next keypress, whatever it is, so the
    // hotkey table does not also act on it.
    if (capture_ >= 0 && event.type == SDL_KEYDOWN && !event.key.repeat) {
        const SDL_Scancode key = event.key.keysym.scancode;
        if (key == SDL_SCANCODE_ESCAPE) {
            cancelCapture();
            dirty_ = true;
            return true;
        }
        if (hooks_.settings && capture_ >= kKeysPadFirst &&
            capture_ < kKeysPadFirst + emu::frontend::kPadActionCount) {
            hooks_.settings->keyboard_keys[static_cast<std::size_t>(
                capture_ - kKeysPadFirst)] = static_cast<int>(key);
            if (hooks_.persistSettings) hooks_.persistSettings();
        } else if (hooks_.hotkeys && capture_ >= kKeysHotkeyFirst &&
                   capture_ <
                       kKeysHotkeyFirst + emu::frontend::kHotkeyCount) {
            const int index = capture_ - kKeysHotkeyFirst;
            const auto action = emu::frontend::kHotkeys[index].action;
            const std::uint16_t mod =
                emu::frontend::canonicalMod(event.key.keysym.mod);

            // Two actions on one combination means one of them silently stops
            // working, so the clash is reported instead of accepted.
            const auto clash =
                hooks_.hotkeys->conflict(key, mod, action);
            if (clash != emu::frontend::HotkeyAction::None) {
                if (hooks_.notify) {
                    hooks_.notify("That combination is already bound",
                                  Toast::Kind::Error);
                }
                cancelCapture();
                dirty_ = true;
                return true;
            }
            hooks_.hotkeys->bind(action, key, mod);
        }
        cancelCapture();
        dirty_ = true;
        return true;
    }

    if (event.type == SDL_MOUSEMOTION) {
        int x = event.motion.x;
        int y = event.motion.y;
        toRenderCoords(display_scale, &x, &y);

        hot_row_ = -1;
        hot_slot_ = -1;
        if (page_ == OverlayPage::States) {
            for (int slot = 0; slot < 10; ++slot) {
                if (pointInside(slotRectAt(body, metrics, slot), x, y)) {
                    hot_slot_ = slot;
                    break;
                }
            }
        } else {
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                const SDL_Rect rect = rowRectAt(body, metrics, i);
                if (!pointInside(body, x, y)) break;
                if (pointInside(rect, x, y)) {
                    hot_row_ = i;
                    break;
                }
            }
        }
        return false;
    }

    if (event.type == SDL_MOUSEWHEEL) {
        if (page_ != OverlayPage::States) {
            scroll_ -= event.wheel.y * metrics.row_height();
            clampScroll(body, metrics);
        }
        return true;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        int x = event.button.x;
        int y = event.button.y;
        toRenderCoords(display_scale, &x, &y);

        // A click outside the panel closes it, which is what every other
        // modal in the app does.
        if (!pointInside(panel, x, y)) {
            close();
            if (hooks_.resume) hooks_.resume();
            return true;
        }

        const SDL_Rect tabs = tabsRect(panel, metrics);
        if (pointInside(tabs, x, y)) {
            const int count = static_cast<int>(OverlayPage::Count);
            const int width = tabs.w / count;
            if (width > 0) {
                const int index = std::clamp((x - tabs.x) / width, 0,
                                             count - 1);
                if (index != static_cast<int>(page_)) {
                    page_ = static_cast<OverlayPage>(index);
                    selected_ = 0;
                    scroll_ = 0;
                    capture_ = -1;
                    dirty_ = true;
                }
            }
            return true;
        }

        if (page_ == OverlayPage::States) {
            for (int slot = 0; slot < 10; ++slot) {
                if (!pointInside(slotRectAt(body, metrics, slot), x, y)) {
                    continue;
                }
                slot_ = slot;
                if (hooks_.saveSlot) hooks_.saveSlot(slot);
                dirty_ = true;
                return true;
            }
            return true;
        }

        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            if (!pointInside(body, x, y)) break;
            if (!pointInside(rowRectAt(body, metrics, i), x, y)) continue;
            const Row& row = rows_[static_cast<std::size_t>(i)];
            if (row.header || !row.enabled) return true;
            selected_ = i;
            activate();
            return true;
        }
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void GameOverlay::drawRows(SDL_Renderer* renderer, Painter& painter,
                           TextCache& text, const Theme& theme,
                           const Metrics& m, const SDL_Rect& body) {
    const SDL_Rect previous = painter.pushClip(body);

    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const SDL_Rect rect = rowRectAt(body, m, i);
        // Rows scrolled out of view are skipped rather than clipped, because
        // each one would otherwise still cost a text rasterization.
        if (rect.y + rect.h < body.y || rect.y > body.y + body.h) continue;

        const Row& row = rows_[static_cast<std::size_t>(i)];
        const bool selected = i == selected_ && !row.header;
        const bool hot = i == hot_row_ && !row.header && row.enabled;

        if (selected) {
            painter.roundedRect(inset(rect, m.px(2)), m.radius_small(),
                                theme.surface_active);
        } else if (hot) {
            painter.roundedRect(inset(rect, m.px(2)), m.radius_small(),
                                theme.surface_hover);
        }

        const int text_y = rect.y + (rect.h - text.lineHeight(FontRole::Body)) / 2;
        const SDL_Color label_color =
            row.header ? theme.text_muted
                       : (row.enabled ? theme.text : theme.text_muted);

        text.draw(renderer, row.header ? FontRole::Small : FontRole::Body,
                  text.ellipsize(FontRole::Body, row.label, body.w * 2 / 3),
                  rect.x + m.pad(), text_y, label_color);

        if (row.value.empty()) continue;

        // An armed row shows what it is waiting for in place of its value,
        // because the value it currently holds is about to be replaced.
        const bool armed = capture_ == row.id;
        const std::string value = armed ? "Press a key or button" : row.value;
        text.drawRight(renderer, FontRole::Body,
                       text.ellipsize(FontRole::Body, value, body.w / 2),
                       rect.x + rect.w - m.pad(), text_y,
                       armed ? theme.accent : theme.text_muted);
    }

    painter.popClip(previous);

    const int content = static_cast<int>(rows_.size()) * m.row_height();
    if (content > body.h) {
        drawScrollbar(renderer, painter, theme, m,
                      SDL_Rect{body.x + body.w - m.scrollbar_width(), body.y,
                               m.scrollbar_width(), body.h},
                      content, body.h, scroll_);
    }
}

void GameOverlay::drawStates(SDL_Renderer* renderer, Painter& painter,
                             TextCache& text, const Theme& theme,
                             const Metrics& m, const SDL_Rect& body) {
    for (int slot = 0; slot < 10; ++slot) {
        const SDL_Rect rect = slotRectAt(body, m, slot);
        const bool selected = slot == slot_;
        const bool hot = slot == hot_slot_;

        painter.roundedRectBordered(
            rect, m.radius_small(),
            selected ? theme.surface_active
                     : (hot ? theme.surface_hover : theme.surface),
            selected ? theme.accent : theme.border, m.border());

        const SlotView view =
            hooks_.slotView ? hooks_.slotView(slot) : SlotView{};

        // The thumbnail fills the cell above the caption. Without one the
        // cell says so rather than showing an empty frame, which reads as a
        // broken image.
        const SDL_Rect image{rect.x + m.px(4), rect.y + m.px(4),
                             rect.w - m.px(8), rect.h - m.px(26)};
        if (view.exists && view.thumbnail) {
            SDL_RenderCopy(renderer, view.thumbnail, nullptr, &image);
        } else {
            painter.roundedRect(image, m.radius_small(), theme.bg);
            text.drawCentered(renderer, FontRole::Small,
                              view.exists ? "No image" : "Empty", image,
                              theme.text_muted);
        }

        const std::string caption =
            "Slot " + std::to_string(slot) +
            (view.exists && !view.when.empty() ? "  " + view.when : "");
        text.draw(renderer, FontRole::Small,
                  text.ellipsize(FontRole::Small, caption, rect.w - m.px(8)),
                  rect.x + m.px(5), rect.y + rect.h - m.px(20),
                  view.exists ? theme.text : theme.text_muted);
    }

    const int hint_y = body.y + body.h - text.lineHeight(FontRole::Small);
    text.draw(renderer, FontRole::Small,
              "Enter saves  -  L loads  -  Delete clears",
              body.x, hint_y, theme.text_muted);
}

void GameOverlay::drawPadPage(SDL_Renderer* renderer, Painter& painter,
                              TextCache& text, const Theme& theme,
                              const Metrics& m, const SDL_Rect& body,
                              std::uint64_t now_ms) {
    // The diagram takes the top half and the binding rows the bottom, so a
    // press is visible at the same time as the row being rebound.
    const int diagram_height = std::min(body.h / 2, m.px(200));
    const SDL_Rect diagram{body.x, body.y, body.w, diagram_height};

    const ControllerSnapshot state =
        hooks_.pad ? hooks_.pad() : ControllerSnapshot{};

    // While a row is armed the diagram highlights the button it is about to
    // take, so the player can see which physical button they are binding.
    SDL_GameControllerButton highlight = SDL_CONTROLLER_BUTTON_INVALID;
    if (capture_ >= kPadFirst &&
        capture_ < kPadFirst + emu::frontend::kPadActionCount) {
        highlight = diagramButton(capture_ - kPadFirst);
    }

    drawController(renderer, painter, text, theme, m, diagram, state,
                   highlight, now_ms);

    const SDL_Rect rows{body.x, diagram.y + diagram.h + m.gap(), body.w,
                        body.y + body.h - (diagram.y + diagram.h + m.gap())};
    if (rows.h > m.row_height()) {
        drawRows(renderer, painter, text, theme, m, rows);
    }
}

void GameOverlay::draw(SDL_Renderer* renderer, Painter& painter,
                       TextCache& text, const Theme& theme,
                       const Metrics& metrics, const SDL_Rect& window,
                       std::uint64_t now_ms) {
    if (!open_) return;
    if (dirty_) rebuild();

    // Dims the game behind the panel. Without it the picture shows through
    // the gaps between rows and the text becomes hard to read on bright
    // scenes.
    painter.rect(window, SDL_Color{0, 0, 0, 150});

    const SDL_Rect panel = panelRect(window, metrics);
    last_panel_ = panel;
    drawPanel(renderer, painter, theme, metrics, panel);

    const SDL_Rect tabs = tabsRect(panel, metrics);
    const int count = static_cast<int>(OverlayPage::Count);
    const int tab_width = count > 0 ? tabs.w / count : tabs.w;
    for (int i = 0; i < count; ++i) {
        const SDL_Rect rect{tabs.x + i * tab_width, tabs.y, tab_width,
                            tabs.h};
        const bool active = i == static_cast<int>(page_);
        if (active) {
            painter.roundedRect(rect, metrics.radius_small(),
                               theme.accent_soft);
        }
        const char* name = overlayPageName(static_cast<OverlayPage>(i));
        text.drawCentered(
            renderer, FontRole::Small,
            text.ellipsize(FontRole::Small, name, tab_width - metrics.px(6)),
            rect, active ? theme.accent : theme.text_muted);
    }

    const SDL_Rect body = bodyRect(panel, metrics);
    last_body_ = body;

    if (page_ != OverlayPage::States) clampScroll(body, metrics);

    switch (page_) {
    case OverlayPage::States:
        drawStates(renderer, painter, text, theme, metrics, body);
        break;
    case OverlayPage::Pad:
        drawPadPage(renderer, painter, text, theme, metrics, body, now_ms);
        break;
    default:
        drawRows(renderer, painter, text, theme, metrics, body);
        break;
    }
}

void drawRewindBar(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                   const Theme& theme, const Metrics& metrics,
                   const SDL_Rect& window, std::size_t depth,
                   std::size_t capacity, float seconds) {
    const int width = std::min(metrics.px(320), window.w - 2 * metrics.pad());
    const int height = metrics.px(52);
    const SDL_Rect panel{window.x + (window.w - width) / 2,
                         window.y + metrics.pad(), width, height};

    painter.roundedRectBordered(panel, metrics.radius(), theme.surface,
                                theme.border, metrics.border());

    char caption[64] = {};
    std::snprintf(caption, sizeof(caption), "Rewinding  %.1f s", seconds);
    text.draw(renderer, FontRole::Small, caption, panel.x + metrics.px(12),
              panel.y + metrics.px(8), theme.text);

    const SDL_Rect bar{panel.x + metrics.px(12),
                       panel.y + height - metrics.px(16),
                       panel.w - 2 * metrics.px(12), metrics.px(6)};
    // Guarded: an empty buffer would divide by zero on the first frame of a
    // rewind, before anything has been captured.
    const float fraction =
        capacity > 0 ? static_cast<float>(depth) / static_cast<float>(capacity)
                     : 0.0f;
    drawProgressBar(renderer, painter, theme, metrics, bar, fraction);
}

}  // namespace emu::frontend::ui
