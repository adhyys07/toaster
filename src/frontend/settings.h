#pragma once
#include <array>
#include <string>
#include <SDL.h>

namespace emu::frontend {

constexpr int kPadActionCount = 12;

enum PadAction {
    PadCross, PadCircle, PadSquare, PadTriangle,
    PadStart, PadSelect,
    PadUp, PadDown, PadLeft, PadRight,
    PadL1, PadR1
};

enum class Upscaler {
    Nearest,
    Linear,
    Fsr1
};

enum class ScalingMode {
    Stretch,
    Integer,
    Aspect
};

struct Settings {
    bool mute = false;
    bool fullscreen = false;
    bool show_fps = false;
    Upscaler upscaler = Upscaler::Nearest;
    float sharpness = 0.2f;
    int volume = 100;
    int audio_buffer_samples = 1024;
    std::string audio_device;
    ScalingMode scaling = ScalingMode::Aspect;
    std::array<int, kPadActionCount> keyboard_keys{
        SDL_SCANCODE_Z,
        SDL_SCANCODE_X,
        SDL_SCANCODE_A,
        SDL_SCANCODE_S,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Q,
        SDL_SCANCODE_W
};

    std::array<int, kPadActionCount> pad_buttons{
        0, 1, 2, 3,       // A, B, X, Y
        6, 4,             // Start, Back
        11, 12, 13, 14,   // D-pad
        9, 10             // L1, R1
    };
};

Settings loadSettings();
bool saveSettings(const Settings& settings);
const char* upscalerName(Upscaler mode);
void cycleUpscaler(Settings& settings);
const char* scalingName(ScalingMode mode);
void cycleScaling(Settings& settings);
void changeVolume(Settings& settings, int delta);

} // namespace emu::frontend