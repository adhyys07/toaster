#pragma once
#include <array>

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

struct Settings {
    bool mute = false;
    bool fullscreen = false;
    bool show_fps = false;
    Upscaler upscaler = Upscaler::Nearest;
    float sharpness = 0.2f;

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

} // namespace emu::frontend