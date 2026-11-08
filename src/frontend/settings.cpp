#include "settings.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <SDL.h>
#include <SDL_opengl.h>
#include <exception>
#include <string>
#include <utility>
#include "libretro.h"

namespace emu::frontend {
namespace {

Upscaler parseUpscaler(const std::string& value) {
    if (value == "linear") return Upscaler::Linear;
    if (value == "fsr1") return Upscaler::Fsr1;
    if (value == "crt") return Upscaler::Crt;
    return Upscaler::Nearest;
}

const char* upscalerValue(Upscaler mode) {
    switch (mode) {
    case Upscaler::Linear: return "linear";
    case Upscaler::Fsr1:   return "fsr1";
    case Upscaler::Crt:    return "crt";
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

bool validControllerButton(int button) {
    return button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX;
}

bool parseControllerButton(const std::string& value, int& button) {
    try {
        const int parsed = std::stoi(value);
        if (!validControllerButton(parsed)) return false;
        button = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void writeControllerButton(std::ofstream& file,
                           const char* name,
                           int button) {
    file << name << '=' << button << '\n';
}

} // namespace

int16_t scaleAudioSample(int16_t sample, int volume) {
    const int scaled = static_cast<int>(
        static_cast<float>(sample) * std::clamp(volume, 0, 100) / 100.0f);
    return static_cast<int16_t>(std::clamp(scaled, -32768, 32767));
}

void applyAudioVolume(int16_t* samples, size_t sample_count, int volume) {
    if (!samples) return;
    for (size_t i = 0; i < sample_count; ++i)
        samples[i] = scaleAudioSample(samples[i], volume);
}

void applyMute(int16_t* samples, size_t sample_count, bool muted) {
    if (muted && samples) std::fill(samples, samples + sample_count, 0);
}

std::vector<std::string> listAudioDevices() {
    std::vector<std::string> devices;
    const int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(i, 0))
            devices.emplace_back(name);
    }
    return devices;
}

SDL_Rect calculateViewport(int window_width, int window_height,
                           int game_width, int game_height,
                           double aspect_ratio, ScalingMode mode) {
    if (game_width <= 0 || game_height <= 0 || window_width <= 0 ||
        window_height <= 0) {
        return {0, 0, window_width, window_height};
    }
    if (mode == ScalingMode::Stretch)
        return {0, 0, window_width, window_height};

    if (mode == ScalingMode::Integer) {
        const int scale = std::max(1, std::min(window_width / game_width,
                                               window_height / game_height));
        const int width = game_width * scale;
        const int height = game_height * scale;
        return {(window_width - width) / 2, (window_height - height) / 2,
                width, height};
    }

    const double aspect = aspect_ratio > 0.0
        ? aspect_ratio
        : static_cast<double>(game_width) / game_height;
    int width = window_width;
    int height = static_cast<int>(width / aspect);
    if (height > window_height) {
        height = window_height;
        width = static_cast<int>(height * aspect);
    }
    return {(window_width - width) / 2, (window_height - height) / 2,
            width, height};
}

SDL_AudioDeviceID openAudioDevice(
    const AudioSettings& settings,
    SDL_AudioSpec& desired,
    SDL_AudioSpec& obtained) {
    desired = {};
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = static_cast<Uint16>(
        std::clamp(settings.buffer_samples, 256, 4096));
    const char* device = settings.device.empty() ? nullptr : settings.device.c_str();
    return SDL_OpenAudioDevice(device, 0, &desired, &obtained, 0);
}

GameSettings loadGameSettings(const std::string& game_path) {
    GameSettings settings;
    std::ifstream file(game_path + ".ini");
    if (!file) return settings;

    std::string line;
    while (std::getline(file, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        try {
            if (key == "scale") {
                settings.scale = std::clamp(std::stoi(value), 1, 8);
                settings.present.scale = true;
            } else if (key == "integer_scale") {
                settings.integer_scale = value == "true";
                settings.present.integer_scale = true;
            } else if (key == "bilinear_filter") {
                settings.bilinear_filter = value == "true";
                settings.present.bilinear_filter = true;
            } else if (key == "vsync") {
                settings.vsync = value == "true";
                settings.present.vsync = true;
            } else if (key == "show_fps") {
                settings.show_fps = value == "true";
                settings.present.show_fps = true;
            } else if (key == "mute_audio") {
                settings.mute_audio = value == "true";
                settings.present.mute_audio = true;
            } else if (key == "fast_forward_speed") {
                settings.fast_forward_speed =
                    std::clamp(std::stoi(value), 1, 8);
                settings.present.fast_forward_speed = true;
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    return settings;
}

bool saveGameSettings(const std::string& game_path,
                      const GameSettings& settings) {
    if (!settings.present.scale && !settings.present.integer_scale &&
        !settings.present.bilinear_filter && !settings.present.vsync &&
        !settings.present.show_fps && !settings.present.mute_audio &&
        !settings.present.fast_forward_speed) {
        std::error_code ec;
        std::filesystem::remove(game_path + ".ini", ec);
        return !ec;
    }

    std::ofstream file(game_path + ".ini", std::ios::trunc);
    if (!file) return false;

    if (settings.present.scale) {
        file << "scale=" << std::clamp(settings.scale, 1, 8) << '\n';
    }
    if (settings.present.integer_scale) {
        file << "integer_scale="
             << (settings.integer_scale ? "true" : "false") << '\n';
    }
    if (settings.present.bilinear_filter) {
        file << "bilinear_filter="
             << (settings.bilinear_filter ? "true" : "false") << '\n';
    }
    if (settings.present.vsync) {
        file << "vsync=" << (settings.vsync ? "true" : "false") << '\n';
    }
    if (settings.present.show_fps) {
        file << "show_fps=" << (settings.show_fps ? "true" : "false") << '\n';
    }
    if (settings.present.mute_audio) {
        file << "mute_audio=" << (settings.mute_audio ? "true" : "false")
             << '\n';
    }
    if (settings.present.fast_forward_speed) {
        file << "fast_forward_speed="
             << std::clamp(settings.fast_forward_speed, 1, 8) << '\n';
    }

    return file.good();
}


bool ControllerProfileManager::validate(
    const ControllerProfile& profile,
    std::string& error) const {
    if (profile.name.empty()) {
        error = "Controller profile name cannot be empty.";
        return false;
    }

    const int buttons[] = {
        profile.cross, profile.circle, profile.square, profile.triangle,
        profile.start, profile.back, profile.l1, profile.r1,
        profile.l2, profile.r2
    };
    for (const int button : buttons) {
        if (!validControllerButton(button)) {
            error = "Controller profile contains an invalid button.";
            return false;
        }
    }
    return true;
}

bool ControllerProfileManager::save(
    const ControllerProfile& profile,
    const std::filesystem::path& file_path,
    std::string& error) {
    if (!validate(profile, error)) return false;

    std::error_code filesystem_error;
    const auto parent = file_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            error = "Could not create controller-profile directory: " +
                    filesystem_error.message();
            return false;
        }
    }

    std::ofstream file(file_path, std::ios::trunc);
    if (!file) {
        error = "Could not open controller-profile file.";
        return false;
    }

    file << "name=" << profile.name << '\n';
    writeControllerButton(file, "cross", profile.cross);
    writeControllerButton(file, "circle", profile.circle);
    writeControllerButton(file, "square", profile.square);
    writeControllerButton(file, "triangle", profile.triangle);
    writeControllerButton(file, "start", profile.start);
    writeControllerButton(file, "back", profile.back);
    writeControllerButton(file, "l1", profile.l1);
    writeControllerButton(file, "r1", profile.r1);
    writeControllerButton(file, "l2", profile.l2);
    writeControllerButton(file, "r2", profile.r2);

    if (!file.good()) {
        error = "Could not write controller-profile file.";
        return false;
    }
    return true;
}

bool ControllerProfileManager::load(
    const std::filesystem::path& file_path,
    ControllerProfile& profile,
    std::string& error) {
    std::ifstream file(file_path);
    if (!file) {
        error = "Could not open controller-profile file.";
        return false;
    }

    ControllerProfile loaded;
    std::string line;
    while (std::getline(file, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "name") {
            loaded.name = value;
        } else if (key == "cross") {
            parseControllerButton(value, loaded.cross);
        } else if (key == "circle") {
            parseControllerButton(value, loaded.circle);
        } else if (key == "square") {
            parseControllerButton(value, loaded.square);
        } else if (key == "triangle") {
            parseControllerButton(value, loaded.triangle);
        } else if (key == "start") {
            parseControllerButton(value, loaded.start);
        } else if (key == "back") {
            parseControllerButton(value, loaded.back);
        } else if (key == "l1") {
            parseControllerButton(value, loaded.l1);
        } else if (key == "r1") {
            parseControllerButton(value, loaded.r1);
        } else if (key == "l2") {
            parseControllerButton(value, loaded.l2);
        } else if (key == "r2") {
            parseControllerButton(value, loaded.r2);
        }
    }

    if (!file.good() && !file.eof()) {
        error = "Could not read controller-profile file.";
        return false;
    }
    if (!validate(loaded, error)) return false;

    profile = loaded;
    return true;
}

bool configureVsync(const VideoSettings& settings,
                    std::string& error) {
    if (SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0) != 0) {
        error = "Could not configure VSync: ";
        error += SDL_GetError();
        return false;
    }
    return true;
}

void applyControllerProfile(
    const ControllerProfile& profile,
    const SDL_GameController* controller,
    uint16_t& joypad_state) {
    if (!controller) return;

    const auto pressed = [controller](int button) {
        return SDL_GameControllerGetButton(
            const_cast<SDL_GameController*>(controller),
            static_cast<SDL_GameControllerButton>(button)) != 0;
    };
    const auto set = [&joypad_state](unsigned id, bool value) {
        if (value) joypad_state = static_cast<uint16_t>(
            joypad_state | (1u << id));
    };

    set(RETRO_DEVICE_ID_JOYPAD_B, pressed(profile.cross));
    set(RETRO_DEVICE_ID_JOYPAD_A, pressed(profile.circle));
    set(RETRO_DEVICE_ID_JOYPAD_Y, pressed(profile.square));
    set(RETRO_DEVICE_ID_JOYPAD_X, pressed(profile.triangle));
    set(RETRO_DEVICE_ID_JOYPAD_START, pressed(profile.start));
    set(RETRO_DEVICE_ID_JOYPAD_SELECT, pressed(profile.back));
    set(RETRO_DEVICE_ID_JOYPAD_L, pressed(profile.l1));
    set(RETRO_DEVICE_ID_JOYPAD_R, pressed(profile.r1));
}

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
        } else if (key == "vsync") {
            settings.vsync = value == "true" || value == "1";
        } else if (key == "upscaler") {
            settings.upscaler = parseUpscaler(value);
        }  else if (key == "theme") settings.theme = value;
        else if (key == "input_display") settings.input_display = value == "true";
        else if (key == "volume") {
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
        } else if (key == "rewind_seconds") {
            try {
                settings.rewind_seconds = std::clamp(std::stoi(value), 0, 120);
            } catch (...) {
                settings.rewind_seconds = 10;
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
        }         else if (key == "run_ahead_frames") {
            try {
                settings.run_ahead_frames =
                    std::clamp(std::stoi(value), 1, 8);
            } catch (...) {
                settings.run_ahead_frames = 1;
            }
        } else if (key == "turbo_mask") {
            try {
                // A decimal bitmask over PadAction indices. Appending a new
                // pad action to the enum therefore cannot change the
                // meaning of bits already written to settings.ini.
                settings.turbo_mask =
                    static_cast<uint32_t>(std::stoul(value));
            } catch (...) {
                settings.turbo_mask = 0;
            }
        } else if (key == "turbo_period") {
            try {
                settings.turbo_period = std::clamp(std::stoi(value), 2, 30);
            } catch (...) {
                settings.turbo_period = 4;
            }
        } else if (key == "rumble") {
            settings.rumble = value == "true" || value == "1";
        } else if (key == "pause_on_focus_loss") {
            settings.pause_on_focus_loss =
                value == "true" || value == "1";
        } else if (key == "palette") {
            settings.palette = value;
        } else if (key == "slow_motion_divisor") {
            try {
                settings.slow_motion_divisor =
                    std::clamp(std::stoi(value), 2, 8);
            } catch (...) {
                settings.slow_motion_divisor = 4;
            }
        } else if (key == "audio_channel_mask") {
            try {
                settings.audio_channel_mask =
                    static_cast<uint32_t>(std::stoul(value));
            } catch (...) {
                settings.audio_channel_mask = 0xFFFFFFFFu;
            }
        } else if (key == "crop_overscan") {
            settings.crop_overscan = value == "true" || value == "1";
        } else if (key == "save_backups") {
            try {
                settings.save_backups = std::clamp(std::stoi(value), 0, 9);
            } catch (...) {
                settings.save_backups = 3;
            }
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
         << "vsync=" << (settings.vsync ? "true" : "false") << '\n'
         << "upscaler=" << upscalerValue(settings.upscaler) << '\n'
         << "scaling=" << scalingValue(settings.scaling) << '\n'
        << "sharpness=" << std::clamp(settings.sharpness, 0.0f, 1.0f) << '\n'
        << "rewind_seconds=" << std::clamp(settings.rewind_seconds, 0, 120) << '\n'
        << "volume=" << std::clamp(settings.volume, 0, 100) << '\n'
        << "audio_buffer_samples="
        << std::clamp(settings.audio_buffer_samples, 256, 4096) << '\n'
        << "audio_device=" << settings.audio_device << '\n';
    
    file << "run_ahead_frames=" << std::clamp(settings.run_ahead_frames, 1, 8)
         << '\n';
    file << "turbo_mask=" << settings.turbo_mask << '\n';
    file << "turbo_period=" << std::clamp(settings.turbo_period, 2, 30)
         << '\n';
    file << "rumble=" << (settings.rumble ? "true" : "false") << '\n';
    file << "pause_on_focus_loss="
         << (settings.pause_on_focus_loss ? "true" : "false") << '\n';
    file << "palette=" << settings.palette << '\n';
    file << "slow_motion_divisor="
         << std::clamp(settings.slow_motion_divisor, 2, 8) << '\n';
    file << "audio_channel_mask=" << settings.audio_channel_mask
         << '\n';
    file << "crop_overscan="
         << (settings.crop_overscan ? "true" : "false") << '\n';
    file << "save_backups=" << std::clamp(settings.save_backups, 0, 9)
         << '\n';

    file << "input_display="
         << (settings.input_display ? "true" : "false") << '\n';
    file << "theme=" << (settings.theme.empty() ? "midnight" : settings.theme)
         << '\n';

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
    case Upscaler::Crt:    return "CRT (OpenGL required)";
    case Upscaler::Nearest:
    default:               return "Nearest";
    }
}

void cycleUpscaler(Settings& settings) {
    settings.upscaler = static_cast<Upscaler>(
        (static_cast<int>(settings.upscaler) + 1) % kUpscalerCount);
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
