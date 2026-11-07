#include "game_overlay.h"

#include "../save_state_manager.h"
#include "layout.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace emu::frontend::ui {
namespace {

// The save-state grid is five slots wide, so Up and Down have to move by a
// whole row and the cell width has to divide by the same number.
constexpr int kStateColumns = 5;

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

    kAudioMute = 300,
    kAudioVolume,
    kAudioBuffer,

    kPadFirst = 400,
    kKeysPadFirst = 500, 
    kKeysHotkeyFirst = 600,  
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
        if (!hooks_.game_settings) break;
        const emu::frontend::GameSettings& g = *hooks_.game_settings;
        rows_.push_back({"Overrides for this game only", "", kRowNone, true,
                         false});
        // A dash means "not overridden": the file only carries the keys that
        // are set, so there is a real difference between off and absent.
        rows_.push_back({"VSync",
                         g.present.vsync ? onOff(g.vsync) : std::string("-"),
                         kOptionFirst + 0, false, true});
        rows_.push_back({"FPS counter",
                         g.present.show_fps ? onOff(g.show_fps)
                                            : std::string("-"),
                         kOptionFirst + 1, false, true});
        rows_.push_back({"Mute",
                         g.present.mute_audio ? onOff(g.mute_audio)
                                              : std::string("-"),
                         kOptionFirst + 2, false, true});
        rows_.push_back({"Bilinear filter",
                         g.present.bilinear_filter
                             ? onOff(g.bilinear_filter)
                             : std::string("-"),
                         kOptionFirst + 3, false, true});
        rows_.push_back({"Integer scale",
                         g.present.integer_scale ? onOff(g.integer_scale)
                                                 : std::string("-"),
                         kOptionFirst + 4, false, true});
        rows_.push_back({"Window scale",
                         g.present.scale ? std::to_string(g.scale) + "x"
                                         : std::string("-"),
                         kOptionFirst + 5, false, true});
        rows_.push_back({"Fast forward",
                         g.present.fast_forward_speed
                             ? std::to_string(g.fast_forward_speed) + "x"
                             : std::string("-"),
                         kOptionFirst + 6, false, true});
        break;
    }

    case OverlayPage::States:
    case OverlayPage::Count:
    default:
        break;
    }

    // Never leave the cursor on a header or a disabled row.
    if (selected_ >= static_cast<int>(rows_.size())) {
        selected_ = static_cast<int>(rows_.size()) - 1;
    }
    if (selected_ < 0) selected_ = 0;
    while (selected_ < static_cast<int>(rows_.size()) &&
           (rows_[static_cast<std::size_t>(selected_)].header ||
            !rows_[static_cast<std::size_t>(selected_)].enabled)) {
        ++selected_;
    }
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = 0;
}

// --- navigation -------------------------------------------------------------

void GameOverlay::move(int delta) {
    if (page_ == OverlayPage::States) {
        slot_ = std::clamp(slot_ + delta, 0,
                           emu::frontend::SaveStateManager::kSlotCount - 1);
        return;
    }

    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());
    int next = selected_;
    for (int i = 0; i < count; ++i) {
        next += delta > 0 ? 1 : -1;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        const Row& row = rows_[static_cast<std::size_t>(next)];
        if (!row.header && row.enabled) {
            selected_ = next;
            return;
        }
    }
}

void GameOverlay::moveVertical(int delta) {
    move(page_ == OverlayPage::States ? delta * kStateColumns : delta);
}

void GameOverlay::moveHorizontal(int delta, bool from_pad) {
    // On the grid, sideways walks slots. On a row list it changes the value,
    // which is the only way a pad can turn VSync off - except on the
    // Controller page, where every value is a pad binding and the pad must
    // not be able to rewrite the row it is navigating.
    if (page_ == OverlayPage::States) {
        move(delta);
        return;
    }
    if (from_pad && page_ == OverlayPage::Pad) return;
    adjust(delta);
}

void GameOverlay::switchPage(int delta) {
    const int count = static_cast<int>(OverlayPage::Count);
    int next = static_cast<int>(page_);
    next = (next + delta % count + count) % count;
    page_ = static_cast<OverlayPage>(next);
    selected_ = 0;
    scroll_ = 0;
    capture_ = -1;
    dirty_ = true;
}

void GameOverlay::adjust(int delta) {
    const Row* row = selectedRow();
    if (!row || !hooks_.settings) return;
    emu::frontend::Settings& s = *hooks_.settings;
    const int id = row->id;

    switch (id) {
    case kVideoUpscaler: {
        // cycleUpscaler only steps forward, so going backward walks the whole
        // cycle rather than duplicating the enum order here.
        const int steps = delta > 0 ? 1 : emu::frontend::kUpscalerCount - 1;
        for (int i = 0; i < steps; ++i) emu::frontend::cycleUpscaler(s);
        if (hooks_.notify) {
            hooks_.notify("The upscaler changes on the next launch.",
                          Toast::Kind::Info);
        }
        break;
    }
    case kVideoScaling: {
        const int steps = delta > 0 ? 1 : 2;
        for (int i = 0; i < steps; ++i) emu::frontend::cycleScaling(s);
        break;
    }
    case kVideoSharpness:
        s.sharpness = std::clamp(s.sharpness + 0.05f * delta, 0.0f, 1.0f);
        break;
    case kVideoVsync:
        s.vsync = !s.vsync;
        break;
    case kVideoFps:
        s.show_fps = !s.show_fps;
        break;
    case kVideoInputDisplay:
        s.input_display = !s.input_display;
        break;
    case kVideoRewind:
        s.rewind_seconds = std::clamp(s.rewind_seconds + 5 * delta, 0, 120);
        break;
    case kAudioMute:
        s.mute = !s.mute;
        break;
    case kAudioVolume:
        emu::frontend::changeVolume(s, 5 * delta);
        break;
    case kAudioBuffer: {
        // Powers of two only: SDL rounds anything else up and the number on
        // screen would then disagree with the device.
        int samples = s.audio_buffer_samples;
        samples = delta > 0 ? samples * 2 : samples / 2;
        s.audio_buffer_samples = std::clamp(samples, 256, 4096);
        break;
    }
    default:
        if (id >= kPadFirst &&
            id < kPadFirst + emu::frontend::kPadActionCount) {
            const int index = id - kPadFirst;
            int button = s.pad_buttons[index] + delta;
            if (button < 0) button = SDL_CONTROLLER_BUTTON_MAX - 1;
            if (button >= SDL_CONTROLLER_BUTTON_MAX) button = 0;
            s.pad_buttons[index] = button;
        } else if (id >= kCheatFirst && id < kOptionFirst && hooks_.cheats) {
            const std::size_t index =
                static_cast<std::size_t>(id - kCheatFirst);
            if (index < hooks_.cheats->size()) {
                (*hooks_.cheats)[index].enabled =
                    !(*hooks_.cheats)[index].enabled;
                if (hooks_.cheatsChanged) hooks_.cheatsChanged();
            }
        } else if (id >= kOptionFirst && id < kOptionFirst + 7 &&
                   hooks_.game_settings) {
            emu::frontend::GameSettings& g = *hooks_.game_settings;
            // Each override cycles absent -> off -> on -> absent, so there is
            // always a way back to "use the global setting".
            const auto cycleFlag = [delta](bool& present, bool& value) {
                if (!present) {
                    present = true;
                    value = delta > 0;
                } else if (value == (delta > 0)) {
                    present = false;
                } else {
                    value = delta > 0;
                }
            };
            switch (id - kOptionFirst) {
            case 0: cycleFlag(g.present.vsync, g.vsync); break;
            case 1: cycleFlag(g.present.show_fps, g.show_fps); break;
            case 2: cycleFlag(g.present.mute_audio, g.mute_audio); break;
            case 3:
                cycleFlag(g.present.bilinear_filter, g.bilinear_filter);
                break;
            case 4:
                cycleFlag(g.present.integer_scale, g.integer_scale);
                break;
            case 5:
                if (!g.present.scale) {
                    g.present.scale = true;
                    g.scale = 2;
                } else {
                    g.scale += delta;
                    if (g.scale < 1 || g.scale > 8) {
                        g.present.scale = false;
                        g.scale = 2;
                    }
                }
                break;
            case 6:
                if (!g.present.fast_forward_speed) {
                    g.present.fast_forward_speed = true;
                    g.fast_forward_speed = 2;
                } else {
                    g.fast_forward_speed += delta;
                    if (g.fast_forward_speed < 1 ||
                        g.fast_forward_speed > 8) {
                        g.present.fast_forward_speed = false;
                        g.fast_forward_speed = 2;
                    }
                }
                break;
            default:
                break;
            }
            if (hooks_.persistGameOptions) hooks_.persistGameOptions();
        } else {
            return;
        }
        break;
    }

    if (hooks_.applySettings) hooks_.applySettings();
    if (hooks_.persistSettings && id < kOptionFirst) hooks_.persistSettings();
    dirty_ = true;
}

void GameOverlay::activate() {
    if (page_ == OverlayPage::States) {
        if (hooks_.loadSlot && hooks_.loadSlot(slot_)) close();
        return;
    }

    const Row* row = selectedRow();
    if (!row || !row->enabled) return;
    const int id = row->id;

    switch (id) {
    case kPauseResume:
        close();
        if (hooks_.resume) hooks_.resume();
        return;
    case kPauseStates:
        page_ = OverlayPage::States;
        slot_ = 1;
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
        close();
        return;
    default:
        break;
    }

    // Rows on the binding pages arm a capture; everything else toggles.
    if ((id >= kPadFirst && id < kPadFirst + emu::frontend::kPadActionCount) ||
        (id >= kKeysPadFirst &&
         id < kKeysPadFirst + emu::frontend::kPadActionCount) ||
        (id >= kKeysHotkeyFirst &&
         id < kKeysHotkeyFirst + emu::frontend::kHotkeyCount)) {
        capture_ = id;
        return;
    }

    adjust(1);
}

bool GameOverlay::captureButton(SDL_GameControllerButton button) {
    if (capture_ < kPadFirst ||
        capture_ >= kPadFirst + emu::frontend::kPadActionCount ||
        !hooks_.settings) {
        return false;
    }
    if (button == SDL_CONTROLLER_BUTTON_INVALID) return false;

    hooks_.settings->pad_buttons[capture_ - kPadFirst] =
        static_cast<int>(button);
    capture_ = -1;
    dirty_ = true;
    if (hooks_.persistSettings) hooks_.persistSettings();
    if (hooks_.notify) {
        hooks_.notify(std::string("Bound to ") + padButtonLabel(button),
                      Toast::Kind::Success);
    }
    return true;
}

// The pad path. InputMap was written for the library screen, where sideways
// walks the chip strip, so d-pad left/right and the left stick arrive here as
// PreviousTab/NextTab and the shoulders as PageUp/PageDown. The overlay wants
// the opposite: sideways changes a value and the shoulders change the page.
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
    case UiAction::PreviousTab:
        moveHorizontal(-1, true);
        return true;
    case UiAction::Right:
    case UiAction::NextTab:
        moveHorizontal(1, true);
        return true;
    case UiAction::PageUp:
        switchPage(-1);
        return true;
    case UiAction::PageDown:
        switchPage(1);
        return true;
    case UiAction::Confirm:
        activate();
        return true;
    case UiAction::Cancel:
        if (capture_ >= 0) {
            cancelCapture();
        } else if (page_ == OverlayPage::States) {
            page_ = OverlayPage::Pause;
            dirty_ = true;
        } else {
            close();
            if (hooks_.resume) hooks_.resume();
        }
        return true;
    default:
        return false;
    }
}

bool GameOverlay::handleEvent(const SDL_Event& event, const SDL_Rect& window,
                              const Metrics& metrics, float display_scale) {
    if (!open_) return false;
    (void)window;

    if (event.type == SDL_KEYDOWN) {
        // A capture swallows the whole keyboard: the key being bound must not
        // also do whatever it is currently bound to.
        if (capture_ >= kKeysPadFirst && capture_ < kCheatFirst &&
            hooks_.settings) {
            const SDL_Scancode scancode = event.key.keysym.scancode;
            if (scancode == SDL_SCANCODE_ESCAPE) {
                cancelCapture();
                return true;
            }
            if (capture_ < kKeysHotkeyFirst) {
                hooks_.settings->keyboard_keys[capture_ - kKeysPadFirst] =
                    static_cast<int>(scancode);
            } else if (hooks_.hotkeys) {
                const int index = capture_ - kKeysHotkeyFirst;
                const auto action = emu::frontend::kHotkeys[index].action;
                const std::uint16_t mod = emu::frontend::canonicalMod(
                    static_cast<std::uint16_t>(event.key.keysym.mod));
                const auto clash =
                    hooks_.hotkeys->conflict(scancode, mod, action);
                if (clash != emu::frontend::HotkeyAction::None) {
                    // Silently stealing the combination would leave the other
                    // action dead with no sign of why, so the clash is
                    // reported and the binding refused.
                    if (hooks_.notify) {
                        const char* other = "another action";
                        for (int i = 0; i < emu::frontend::kHotkeyCount; ++i) {
                            if (emu::frontend::kHotkeys[i].action == clash) {
                                other = emu::frontend::kHotkeys[i].label;
                                break;
                            }
                        }
                        hooks_.notify(std::string("Already used by ") + other,
                                      Toast::Kind::Error);
                    }
                    cancelCapture();
                    return true;
                }
                hooks_.hotkeys->bind(action, scancode, mod);
            }
            capture_ = -1;
            dirty_ = true;
            if (hooks_.persistSettings) hooks_.persistSettings();
            return true;
        }
        if (capture_ >= 0) {
            // A pad binding is waiting for a pad button, so the keyboard can
            // only cancel it.
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                cancelCapture();
            }
            return true;
        }

        // The keyboard calls these directly rather than going through
        // handleAction, which is the pad path and reads the same UiAction
        // values differently.
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_UP:    moveVertical(-1); return true;
        case SDL_SCANCODE_DOWN:  moveVertical(1); return true;
        case SDL_SCANCODE_LEFT:  moveHorizontal(-1); return true;
        case SDL_SCANCODE_RIGHT: moveHorizontal(1); return true;
        case SDL_SCANCODE_PAGEUP:   switchPage(-1); return true;
        case SDL_SCANCODE_PAGEDOWN: switchPage(1); return true;
        case SDL_SCANCODE_TAB:
            switchPage((event.key.keysym.mod & KMOD_SHIFT) ? -1 : 1);
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!event.key.repeat) handleAction(UiAction::Confirm);
            return true;
        case SDL_SCANCODE_ESCAPE:
            return handleAction(UiAction::Cancel);
        case SDL_SCANCODE_S:
            if (page_ == OverlayPage::States && hooks_.saveSlot) {
                if (hooks_.saveSlot(slot_)) dirty_ = true;
            }
            return true;
        case SDL_SCANCODE_DELETE:
            if (page_ == OverlayPage::States && hooks_.deleteSlot) {
                hooks_.deleteSlot(slot_);
                dirty_ = true;
            }
            return true;
        default:
            // Everything reaching here is still the overlay's: an unbound key
            // must not fall through and reach the game behind it.
            return true;
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        int x = event.motion.x;
        int y = event.motion.y;
        toRenderCoords(display_scale, &x, &y);
        hot_row_ = -1;
        hot_slot_ = -1;
        if (page_ == OverlayPage::States) {
            for (int slot = 0;
                 slot < emu::frontend::SaveStateManager::kSlotCount; ++slot) {
                if (pointInside(slotRectAt(last_body_, metrics, slot), x, y)) {
                    hot_slot_ = slot;
                    break;
                }
            }
        } else {
            const int rows = static_cast<int>(rows_.size());
            for (int i = scroll_; i < rows; ++i) {
                if (pointInside(rowRectAt(last_body_, metrics, i), x, y)) {
                    hot_row_ = i;
                    break;
                }
            }
        }
        return true;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        int x = event.button.x;
        int y = event.button.y;
        toRenderCoords(display_scale, &x, &y);

        // A click outside the panel closes rather than falling through to the
        // game, which would fire whatever that pixel does.
        if (!pointInside(last_panel_, x, y)) {
            close();
            if (hooks_.resume) hooks_.resume();
            return true;
        }

        const SDL_Rect tabs = tabsRect(last_panel_, metrics);
        if (pointInside(tabs, x, y)) {
            const int count = static_cast<int>(OverlayPage::Count);
            const int width = std::max(1, tabs.w / count);
            const int index = std::clamp((x - tabs.x) / width, 0, count - 1);
            if (index != static_cast<int>(page_)) {
                page_ = static_cast<OverlayPage>(index);
                selected_ = 0;
                scroll_ = 0;
                capture_ = -1;
                dirty_ = true;
            }
            return true;
        }

        if (page_ == OverlayPage::States) {
            if (hot_slot_ >= 0) {
                slot_ = hot_slot_;
                if (hooks_.loadSlot && hooks_.loadSlot(slot_)) close();
            }
            return true;
        }

        if (hot_row_ >= 0 && hot_row_ < static_cast<int>(rows_.size())) {
            const Row& row = rows_[static_cast<std::size_t>(hot_row_)];
            if (!row.header && row.enabled) {
                selected_ = hot_row_;
                activate();
            }
        }
        return true;
    }

    if (event.type == SDL_MOUSEWHEEL) {
        scroll_ = std::max(0, scroll_ - event.wheel.y);
        return true;
    }

    return false;
}

// --- geometry ---------------------------------------------------------------

SDL_Rect overlayPanelRect(const SDL_Rect& window, const Metrics& m) {
    const int width = std::min(m.px(760), window.w - 2 * m.pad());
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
    // The footer hint sits on the last line, so the body stops short of it.
    const int bottom = panel.y + panel.h - m.pad() - m.px(20);
    return SDL_Rect{panel.x + m.pad(), top, panel.w - 2 * m.pad(),
                    std::max(m.row_height(), bottom - top)};
}

SDL_Rect overlaySlotRect(const SDL_Rect& body, const Metrics& m, int slot) {
    const int gap = m.gap();
    const int cell_w = (body.w - (kStateColumns - 1) * gap) / kStateColumns;
    // A 4:3 thumbnail plus two text lines.
    const int cell_h = cell_w * 3 / 4 + m.px(34);
    const int column = slot % kStateColumns;
    const int row = slot / kStateColumns;
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

SDL_Rect GameOverlay::slotRectAt(const SDL_Rect& body, const Metrics& m,
                                 int slot) const {
    return overlaySlotRect(body, m, slot);
}

SDL_Rect GameOverlay::rowRectAt(const SDL_Rect& body, const Metrics& m,
                                int index) const {
    const int y = body.y + (index - scroll_) * m.row_height();
    return SDL_Rect{body.x, y, body.w, m.row_height()};
}

int GameOverlay::visibleRows(const SDL_Rect& body, const Metrics& m) const {
    return std::max(1, body.h / m.row_height());
}

void GameOverlay::clampScroll(const SDL_Rect& body, const Metrics& m) {
    const int visible = visibleRows(body, m);
    const int count = static_cast<int>(rows_.size());
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, count - visible));
}

// --- drawing ----------------------------------------------------------------

void GameOverlay::drawRows(SDL_Renderer* renderer, Painter& painter,
                           TextCache& text, const Theme& theme,
                           const Metrics& m, const SDL_Rect& body) {
    const SDL_Rect previous = painter.pushClip(body);
    const int visible = visibleRows(body, m);
    const int count = static_cast<int>(rows_.size());
    const int text_h = text.lineHeight(FontRole::Body);

    for (int i = scroll_; i < std::min(count, scroll_ + visible); ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        const SDL_Rect rect = rowRectAt(body, m, i);

        if (row.header) {
            text.draw(renderer, FontRole::Small, row.label, rect.x,
                      rect.y + (rect.h - text.lineHeight(FontRole::Small)) / 2,
                      theme.text_muted);
            continue;
        }

        const bool selected = i == selected_;
        const bool armed = selected && capturing();
        if (selected) {
            painter.roundedRect(rect, m.radius_small(),
                                armed ? theme.accent_soft
                                      : theme.surface_active);
        } else if (i == hot_row_) {
            painter.roundedRect(rect, m.radius_small(), theme.surface_hover);
        }

        const int text_y = rect.y + (rect.h - text_h) / 2;
        text.draw(renderer, FontRole::Body,
                  text.ellipsize(FontRole::Body, row.label, rect.w / 2),
                  rect.x + m.px(12), text_y,
                  row.enabled ? theme.text : theme.text_muted);

        const std::string value = armed ? std::string("Press a key...")
                                        : row.value;
        if (!value.empty()) {
            text.drawRight(renderer, FontRole::Body, value,
                           rect.x + rect.w - m.px(12), text_y,
                           armed ? theme.accent
                                 : (row.enabled ? theme.text_muted
                                                : theme.border));
        }
    }

    painter.popClip(previous);

    if (count > visible) {
        const SDL_Rect track{body.x + body.w - m.scrollbar_width(), body.y,
                             m.scrollbar_width(), body.h};
        drawScrollbar(renderer, painter, theme, m, track,
                      count * m.row_height(), body.h,
                      scroll_ * m.row_height());
    }
}

void GameOverlay::drawStates(SDL_Renderer* renderer, Painter& painter,
                             TextCache& text, const Theme& theme,
                             const Metrics& m, const SDL_Rect& body) {
    const int small_h = text.lineHeight(FontRole::Small);

    for (int slot = 0; slot < emu::frontend::SaveStateManager::kSlotCount;
         ++slot) {
        const SDL_Rect cell = overlaySlotRect(body, m, slot);
        const bool selected = slot == slot_;
        const SlotView view = hooks_.slotView ? hooks_.slotView(slot)
                                              : SlotView{};

        painter.roundedRectBordered(
            cell, m.radius_small(),
            selected ? theme.surface_active
                     : (slot == hot_slot_ ? theme.surface_hover
                                          : theme.surface),
            selected ? theme.accent : theme.border, m.border());

        const int art_h = cell.w * 3 / 4;
        const SDL_Rect art{cell.x + m.border(), cell.y + m.border(),
                           cell.w - 2 * m.border(), art_h};
        if (view.thumbnail) {
            SDL_RenderCopy(renderer, view.thumbnail, nullptr, &art);
        } else {
            painter.rect(art, theme.bg);
            text.drawCentered(renderer, FontRole::Small,
                              view.exists ? "No image" : "Empty", art,
                              theme.text_muted);
        }

        const std::string label = slot == 0 ? std::string("Auto")
                                            : "Slot " + std::to_string(slot);
        const int label_y = art.y + art.h + m.px(4);
        text.draw(renderer, FontRole::Small, label, cell.x + m.px(6), label_y,
                  view.exists ? theme.text : theme.text_muted);
        if (!view.when.empty()) {
            text.draw(renderer, FontRole::Small,
                      text.ellipsize(FontRole::Small, view.when,
                                     cell.w - m.px(12)),
                      cell.x + m.px(6), label_y + small_h, theme.text_muted);
        }
    }
}

void GameOverlay::drawPadPage(SDL_Renderer* renderer, Painter& painter,
                              TextCache& text, const Theme& theme,
                              const Metrics& m, const SDL_Rect& body,
                              std::uint64_t now_ms) {
    // The diagram takes the right-hand two fifths and the binding list keeps
    // the rest. This is the screen the controller view was drawn for: the
    // highlight marks the control the selected row is bound to, so the
    // picture answers "which button is Cross?" directly.
    const int diagram_w = std::max(m.px(170), body.w * 2 / 5);
    const SDL_Rect list{body.x, body.y,
                        std::max(m.px(80), body.w - diagram_w - m.gap()),
                        body.h};
    const SDL_Rect diagram{body.x + body.w - diagram_w, body.y, diagram_w,
                           body.h};

    drawRows(renderer, painter, text, theme, m, list);

    SDL_GameControllerButton highlight = SDL_CONTROLLER_BUTTON_INVALID;
    if (const Row* row = selectedRow()) {
        if (row->id >= kPadFirst &&
            row->id < kPadFirst + emu::frontend::kPadActionCount) {
            const int index = row->id - kPadFirst;
            const int button = hooks_.settings
                ? hooks_.settings->pad_buttons[index]
                : static_cast<int>(diagramButton(index));
            if (button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX) {
                highlight = static_cast<SDL_GameControllerButton>(button);
            }
        }
    }

    const ControllerSnapshot state = hooks_.pad ? hooks_.pad()
                                                : ControllerSnapshot{};
    drawController(renderer, painter, text, theme, m, diagram, state,
                   highlight, now_ms);
}

void GameOverlay::draw(SDL_Renderer* renderer, Painter& painter,
                       TextCache& text, const Theme& theme, const Metrics& m,
                       const SDL_Rect& window, std::uint64_t now_ms) {
    if (!open_) return;
    if (dirty_) rebuild();

    // Dim the game rather than cover it: seeing what is behind the menu is
    // the point of an in-game overlay.
    painter.rect(window, withAlpha(theme.bg, 205));

    const SDL_Rect panel = overlayPanelRect(window, m);
    last_panel_ = panel;
    painter.shadow(panel, m.radius(), m.px(6), theme.shadow);
    painter.roundedRectBordered(panel, m.radius(), theme.surface, theme.border,
                                m.border());

    const SDL_Rect tabs = overlayTabsRect(panel, m);
    const int tab_count = static_cast<int>(OverlayPage::Count);
    const int tab_w = std::max(1, tabs.w / tab_count);
    for (int i = 0; i < tab_count; ++i) {
        const SDL_Rect tab{tabs.x + i * tab_w, tabs.y,
                           std::max(1, tab_w - m.px(2)), tabs.h};
        const bool active = i == static_cast<int>(page_);
        if (active) painter.roundedRect(tab, m.radius_small(), theme.accent);
        text.drawCentered(renderer, FontRole::Small,
                          overlayPageName(static_cast<OverlayPage>(i)), tab,
                          active ? theme.text_on_accent : theme.text_muted);
    }

    const SDL_Rect body = overlayBodyRect(panel, m);
    last_body_ = body;
    clampScroll(body, m);

    switch (page_) {
    case OverlayPage::States:
        drawStates(renderer, painter, text, theme, m, body);
        break;
    case OverlayPage::Pad:
        drawPadPage(renderer, painter, text, theme, m, body, now_ms);
        break;
    default:
        drawRows(renderer, painter, text, theme, m, body);
        break;
    }

    const char* hint = page_ == OverlayPage::States
        ? "Enter load   S save   Del delete   Tab page   Esc back"
        : "Arrows move and change   Enter select   Tab page   Esc resume";
    text.draw(renderer, FontRole::Small, hint, panel.x + m.pad(),
              panel.y + panel.h - m.pad() -
                  text.lineHeight(FontRole::Small),
              theme.text_muted);
}

// --- rewind read-out --------------------------------------------------------

void drawRewindBar(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                   const Theme& theme, const Metrics& m,
                   const SDL_Rect& window, std::size_t depth,
                   std::size_t capacity, float seconds) {
    if (capacity == 0) return;

    const int small_h = text.lineHeight(FontRole::Small);
    const int width = std::min(m.px(320), window.w - 2 * m.pad());
    const SDL_Rect box{window.x + (window.w - width) / 2,
                       window.y + m.pad(), width,
                       m.px(14) + small_h + m.px(6)};

    painter.roundedRect(box, m.radius_small(), withAlpha(theme.bg, 200));

    const SDL_Rect bar{box.x + m.px(10), box.y + m.px(8),
                       box.w - 2 * m.px(10), m.px(6)};
    const float fraction = static_cast<float>(depth) /
                           static_cast<float>(capacity);
    drawProgressBar(renderer, painter, theme, m, bar, fraction);

    char label[64] = {};
    std::snprintf(label, sizeof(label), "Rewinding   %.1fs left",
                  static_cast<double>(seconds));
    text.draw(renderer, FontRole::Small, label, bar.x,
              bar.y + bar.h + m.px(3), theme.text_muted);
}

} // namespace emu::frontend::ui
