#pragma once

namespace emu::frontend {

enum class Upscaler {
    Nearest,
    Linear,
    Fsr1
};

struct Settings {
    bool mute = false;
    bool fullscreen = false;
    Upscaler upscaler = Upscaler::Nearest;
    float sharpness = 0.2f;
};

Settings loadSettings();
bool saveSettings(const Settings& settings);
const char* upscalerName(Upscaler mode);
void cycleUpscaler(Settings& settings);

} // namespace emu::frontend