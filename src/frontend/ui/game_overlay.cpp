#include "game_overlay.h"

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
    kOptionFirst = 800
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
            
    }
}