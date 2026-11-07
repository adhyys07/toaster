#include "hotkeys.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace emu::frontend {

const HotkeySpec kHotkeys[] = {
    {"pause_menu", HotkeyAction::PauseMenu, "Pause menu",
     SDL_SCANCODE_F1, kModNone},
    {"state_menu", HotkeyAction::StateMenu, "Save states",
     SDL_SCANCODE_F3, kModNone},
    {"toggle_pause", HotkeyAction::TogglePause, "Pause / resume",
     SDL_SCANCODE_P, kModNone},
    {"frame_advance", HotkeyAction::FrameAdvance, "Advance one frame",
     SDL_SCANCODE_N, kModNone},
    {"fast_forward", HotkeyAction::FastForward, "Fast forward",
     SDL_SCANCODE_TAB, kModNone},
    {"quick_save", HotkeyAction::QuickSave, "Quick save",
     SDL_SCANCODE_F5, kModNone},
    {"quick_load", HotkeyAction::QuickLoad, "Quick load",
     SDL_SCANCODE_F8, kModNone},
    {"screenshot", HotkeyAction::Screenshot, "Screenshot",
     SDL_SCANCODE_F10, kModNone},
    {"gallery", HotkeyAction::Gallery, "Screenshot gallery",
     SDL_SCANCODE_F10, kModShift},
    {"toggle_cheats", HotkeyAction::ToggleCheats, "Toggle cheats",
     SDL_SCANCODE_F9, kModNone},
    {"debug_info", HotkeyAction::DebugInfo, "Print debug info",
     SDL_SCANCODE_F12, kModNone},
    {"volume_down", HotkeyAction::VolumeDown, "Volume down",
     SDL_SCANCODE_MINUS, kModNone},
    {"volume_up", HotkeyAction::VolumeUp, "Volume up",
     SDL_SCANCODE_EQUALS, kModNone},
    {"toggle_mute", HotkeyAction::ToggleMute, "Mute",
     SDL_SCANCODE_M, kModNone},
    {"toggle_fullscreen", HotkeyAction::ToggleFullscreen, "Fullscreen",
     SDL_SCANCODE_F11, kModNone},
    {"toggle_fps", HotkeyAction::ToggleFps, "FPS counter",
     SDL_SCANCODE_F6, kModNone},
    {"toggle_input_display", HotkeyAction::ToggleInputDisplay,
     "Input display", SDL_SCANCODE_F2, kModNone},
    {"reset", HotkeyAction::Reset, "Reset the game",
     SDL_SCANCODE_R, kModCtrl},
    {"quit", HotkeyAction::Quit, "Quit", SDL_SCANCODE_ESCAPE, kModNone},
};

const int kHotkeyCount = static_cast<int>(sizeof(kHotkeys) /
                                          sizeof(kHotkeys[0]));

std::uint16_t canonicalMod(std::uint16_t sdl_mod) {
    std::uint16_t out = kModNone;
    if (sdl_mod & KMOD_SHIFT) out |= kModShift;
    if (sdl_mod & KMOD_CTRL) out |= kModCtrl;
    if (sdl_mod & KMOD_ALT) out |= kModAlt;
    return out;
}

std::string describeHotkey(const HotkeyBinding& binding) {
    if (!binding.bound()) return "Not bound";

    std::string out;
    if (binding.mod & kModCtrl) out += "Ctrl+";
    if (binding.mod & kModAlt) out += "Alt+";
    if (binding.mod & kModShift) out += "Shift+";

    const char* name = SDL_GetScancodeName(binding.key);
    out += (name && *name) ? name : "?";
    return out;
}

void Hotkeys::reset() {
    for (HotkeyBinding& binding : bindings_) binding = HotkeyBinding{};
    for (int i = 0; i < kHotkeyCount; ++i) {
        bindings_[static_cast<int>(kHotkeys[i].action)] =
            HotkeyBinding{kHotkeys[i].default_key, kHotkeys[i].default_mod};
    }
}

HotkeyAction Hotkeys::resolve(const SDL_Keysym& keysym) const {
    const std::uint16_t mod = canonicalMod(
        static_cast<std::uint16_t>(keysym.mod));

    for (int i = 0; i < kHotkeyCount; ++i) {
        const HotkeyBinding& binding =
            bindings_[static_cast<int>(kHotkeys[i].action)];
        if (!binding.bound()) continue;
        if (binding.key == keysym.scancode && binding.mod == mod) {
            return kHotkeys[i].action;
        }
    }
    return HotkeyAction::None;
}

const HotkeyBinding& Hotkeys::binding(HotkeyAction action) const {
    static const HotkeyBinding none;
    const int index = static_cast<int>(action);
    if (index <= 0 || index >= static_cast<int>(HotkeyAction::Count)) {
        return none;
    }
    return bindings_[index];
}

void Hotkeys::bind(HotkeyAction action, SDL_Scancode key,
                   std::uint16_t mod) {
    const int index = static_cast<int>(action);
    if (index <= 0 || index >= static_cast<int>(HotkeyAction::Count)) return;
    bindings_[index] = HotkeyBinding{key, mod};
}

void Hotkeys::unbind(HotkeyAction action) {
    bind(action, SDL_SCANCODE_UNKNOWN, kModNone);
}

HotkeyAction Hotkeys::conflict(SDL_Scancode key, std::uint16_t mod,
                               HotkeyAction ignore) const {
    if (key == SDL_SCANCODE_UNKNOWN) return HotkeyAction::None;
    for (int i = 0; i < kHotkeyCount; ++i) {
        if (kHotkeys[i].action == ignore) continue;
        const HotkeyBinding& binding =
            bindings_[static_cast<int>(kHotkeys[i].action)];
        if (binding.bound() && binding.key == key && binding.mod == mod) {
            return kHotkeys[i].action;
        }
    }
    return HotkeyAction::None;
}

bool Hotkeys::load(const std::filesystem::path& file, std::string& error) {
    std::ifstream input(file);
    if (!input) {
        error.clear();
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        HotkeyAction action = HotkeyAction::None;
        for (int i = 0; i < kHotkeyCount; ++i) {
            if (key == kHotkeys[i].key) {
                action = kHotkeys[i].action;
                break;
            }
        }
        if (action == HotkeyAction::None) continue;

        if (value == "none") {
            unbind(action);
            continue;
        }

        std::uint16_t mod = kModNone;
        std::string remainder = value;
        for (;;) {
            const auto plus = remainder.find('+');
            if (plus == std::string::npos) break;
            const std::string word = remainder.substr(0, plus);
            if (word == "ctrl") mod |= kModCtrl;
            else if (word == "shift") mod |= kModShift;
            else if (word == "alt") mod |= kModAlt;
            else break;
            remainder = remainder.substr(plus + 1);
        }

        const SDL_Scancode scancode =
            SDL_GetScancodeFromName(remainder.c_str());
        if (scancode == SDL_SCANCODE_UNKNOWN) continue;
        bind(action, scancode, mod);
    }

    error.clear();
    return true;
}

bool Hotkeys::save(const std::filesystem::path& file,
                   std::string& error) const {
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }

    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "Could not write " + file.string();
        return false;
    }

    output << "# Toaster hotkeys. One action per line, as\n"
              "#   action=ctrl+shift+F5\n"
              "# Modifier words are ctrl, shift and alt in any order; the\n"
              "# rest is an SDL scancode name. Use 'none' to unbind.\n";

    for (int i = 0; i < kHotkeyCount; ++i) {
        const HotkeyBinding& b = bindings_[
            static_cast<int>(kHotkeys[i].action)];
        output << kHotkeys[i].key << '=';
        if (!b.bound()) {
            output << "none\n";
            continue;
        }
        if (b.mod & kModCtrl) output << "ctrl+";
        if (b.mod & kModAlt) output << "alt+";
        if (b.mod & kModShift) output << "shift+";
        const char* name = SDL_GetScancodeName(b.key);
        output << ((name && *name) ? name : "Unknown") << '\n';
    }

    if (!output.good()) {
        error = "Could not finish writing " + file.string();
        return false;
    }
    error.clear();
    return true;
}

} // namespace emu::frontend
