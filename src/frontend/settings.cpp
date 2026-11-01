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

ScalingMode parseScaling(const std::string& value) {
    if (value == "stretch") return ScalingMode::Stretch;
    if (value == "integer") return ScalingMode::Integer;
    return ScalingMode::Aspect;
}

const char* scalingValue(ScalingMode mode) {
    switch (mode) {
    case ScalingMode::Stretch: return "stretch";
    case ScalingMode::Integer: return "integer";
    case ScalingMode::Aspect:
    default: return "aspect";
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
        } else if (key == "volume") {
            try {
                settings.volume = std::clamp(std::stoi(value), 0, 100);
            } catch (...) {
                settings.volume = 100;
            }
        } else if (key == "audio_buffer_samples") {
            try {
                settings.audio_buffer_samples = std::max(256, std::stoi(value));
            } catch (...) {
                settings.audio_buffer_samples = 1024;
            }
        } else if (key == "audio_device") {
            settings.audio_device = value;
        } else if (key == "audio_device") {
            settings.audio_device = value;
        } else if (key == "scaling") {
            settings.scaling = parseScaling(value);
        } else if (key.rfind("keyboard_key_", 0) == 0) {
            try {
                const int index = std::stoi(key.substr(13));
                const int scancode = std::stoi(value);

                if (index >= 0 && index < kPadActionCount &&
                    scancode >= 0 &&
                    scancode < SDL_NUM_SCANCODES) {
                    settings.keyboard_keys[index] = scancode;
                }
            } catch (...) {
            }
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
         << "scaling=" << scalingValue(settings.scaling) << '\n'
        << "sharpness=" << std::clamp(settings.sharpness, 0.0f, 1.0f) << '\n'
        << "volume=" << std::clamp(settings.volume, 0, 100) << '\n'
        << "audio_buffer_samples="
        << std::clamp(settings.audio_buffer_samples, 256, 4096) << '\n'
        << "audio_device=" << settings.audio_device << '\n';

    for (int i = 0; i < kPadActionCount; ++i) {
        file << "pad_button_" << i << '=' << settings.pad_buttons[i] << '\n';
        file << "keyboard_key_" << i << '=' << settings.keyboard_keys[i] << '\n';
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

const char* scalingName(ScalingMode mode) {
    switch (mode) {
    case ScalingMode::Stretch: return "Stretch";
    case ScalingMode::Integer: return "Integer";
    case ScalingMode::Aspect:
    default: return "Aspect";
    }
}

void cycleScaling(Settings& settings) {
    switch (settings.scaling) {
    case ScalingMode::Aspect: settings.scaling = ScalingMode::Integer; break;
    case ScalingMode::Integer: settings.scaling = ScalingMode::Stretch; break;
    case ScalingMode::Stretch: settings.scaling = ScalingMode::Aspect; break;
    }
}

void changeVolume(Settings& settings, int delta) {
    settings.volume = std::clamp(settings.volume + delta, 0, 100);
}

} // namespace emu::frontend
