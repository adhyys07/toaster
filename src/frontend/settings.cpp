#include "settings.h"

#include <algorithm>
#include <fstream>
#include <SDL.h>
#include <string>

namespace emu::frontend {
namespace {

Upscaler parseUpscaler(const std::string& value) {
    if (value == "linear") return Upscaler::Linear;
    if (value == "fsr1") return Upscaler::Fsr1;
    return Upscaler::Nearest;
}

const char* upscalerValue(Upscaler mode) {
    switch (mode) {
    case Upscaler::Linear: return "linear";
    case Upscaler::Fsr1:   return "fsr1";
    case Upscaler::Nearest:
    default:               return "nearest";
    }
}

} // namespace

Settings loadSettings() {
    Settings settings;
    std::ifstream file("settings.ini");
    std::string line;

    while (std::getline(file, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "mute") {
            settings.mute = value == "true" || value == "1";
        } else if (key == "fullscreen") {
            settings.fullscreen = value == "true" || value == "1";
        } else if (key == "show_fps") {
            settings.show_fps = value == "true" || value == "1";
        } else if (key == "upscaler") {
            settings.upscaler = parseUpscaler(value);
        } else if (key == "sharpness") {
            try {
                settings.sharpness = std::clamp(std::stof(value), 0.0f, 1.0f);
            } catch (...) {
                settings.sharpness = 0.2f;
            }
        } else if (key.rfind("pad_button_", 0) == 0) {
            try {
                const int index = std::stoi(key.substr(11));
                const int button = std::stoi(value);
                if (index >= 0 && index < kPadActionCount &&
                    button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX) {
                    settings.pad_buttons[index] = button;
                }
            } catch (...) {}
        }
    }

    return settings;
}

bool saveSettings(const Settings& settings) {
    std::ofstream file("settings.ini", std::ios::trunc);
    if (!file) return false;

    file << "mute=" << (settings.mute ? "true" : "false") << '\n'
         << "fullscreen=" << (settings.fullscreen ? "true" : "false") << '\n'
         << "show_fps=" << (settings.show_fps ? "true" : "false") << '\n'
         << "upscaler=" << upscalerValue(settings.upscaler) << '\n'
         << "sharpness=" << std::clamp(settings.sharpness, 0.0f, 1.0f) << '\n';

    for (int i = 0; i < kPadActionCount; ++i) {
        file << "pad_button_" << i << '=' << settings.pad_buttons[i] << '\n';
    }
    return file.good();
}

const char* upscalerName(Upscaler mode) {
    switch (mode) {
    case Upscaler::Linear: return "Linear";
    case Upscaler::Fsr1:   return "FSR 1 (OpenGL required)";
    case Upscaler::Nearest:
    default:               return "Nearest";
    }
}

void cycleUpscaler(Settings& settings) {
    switch (settings.upscaler) {
    case Upscaler::Nearest: settings.upscaler = Upscaler::Linear; break;
    case Upscaler::Linear:  settings.upscaler = Upscaler::Fsr1; break;
    case Upscaler::Fsr1:    settings.upscaler = Upscaler::Nearest; break;
    }
}

} // namespace emu::frontend
