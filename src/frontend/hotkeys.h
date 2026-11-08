#pragma once

#include <SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace emu::frontend {

enum class HotkeyAction {
    None,
    PauseMenu,
    StateMenu,
    TogglePause,
    FrameAdvance,
    FastForward,
    QuickSave,
    QuickLoad,
    Screenshot,
    Gallery,
    ToggleCheats,
    DebugInfo,
    VolumeDown,
    VolumeUp,
    ToggleMute,
    ToggleFullscreen,
    ToggleFps,
    ToggleInputDisplay,
    RecordMovie,
    PlayMovie,
    Reset,
    Quit,
    Count
};

enum HotkeyMod : std::uint16_t {
    kModNone = 0,
    kModShift = 1,
    kModCtrl = 2,
    kModAlt = 4
};

struct HotkeyBinding {
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    std::uint16_t mod = kModNone;

    bool bound() const { return key != SDL_SCANCODE_UNKNOWN; }
};

struct HotkeySpec {
    const char* key;
    HotkeyAction action;
    const char* label;
    SDL_Scancode default_key;
    std::uint16_t default_mod;
};

extern const HotkeySpec kHotkeys[];
extern const int kHotkeyCount;

std::uint16_t canonicalMod(std::uint16_t sdl_mod);

std::string describeHotkey(const HotkeyBinding& binding);

class Hotkeys {
public:
    Hotkeys() { reset(); }
    void reset();

    HotkeyAction resolve(const SDL_Keysym& keysym) const;

    const HotkeyBinding& binding(HotkeyAction action) const;
    void bind(HotkeyAction action, SDL_Scancode key, std::uint16_t mod);
    void unbind(HotkeyAction action);

     HotkeyAction conflict(SDL_Scancode key, std::uint16_t mod,
                          HotkeyAction ignore) const;

    bool load(const std::filesystem::path& file, std::string& error);
    bool save(const std::filesystem::path& file, std::string& error) const;

private:
    HotkeyBinding bindings_[static_cast<int>(HotkeyAction::Count)]{};
};

}