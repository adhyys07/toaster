#pragma once
#include <array>
#include <string>
#include <SDL.h>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <cstddef>

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
    bool vsync = true;
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

struct GameSettings {
    int scale = 2;
    bool integer_scale = true;
    bool bilinear_filter = false;
    bool vsync = true;
    bool show_fps = false;
    bool mute_audio = false;
    int fast_forward_speed = 2;
};

enum class VideoShader {
    None,
    Nearest,
    Bilinear,
    Fsr
};

struct AudioSettings {
    bool muted = false;
    int volume = 100;
    int buffer_samples = 1024;
    std::string device;
};

struct VideoSettings {
    bool vsync = true;
    bool integer_scaling = true;
    bool fullscreen = false;
    bool show_fps = false;
    VideoShader shader = VideoShader::None;
};

SDL_Rect calculateViewport(int window_width, int window_height,
                           int game_width, int game_height,
                           const VideoSettings& settings);

struct ControllerProfile {
    std::string name = "Default";

    int cross = SDL_CONTROLLER_BUTTON_A;
    int circle = SDL_CONTROLLER_BUTTON_B;
    int square = SDL_CONTROLLER_BUTTON_X;
    int triangle = SDL_CONTROLLER_BUTTON_Y;
    int start = SDL_CONTROLLER_BUTTON_START;
    int back = SDL_CONTROLLER_BUTTON_BACK;
    int l1 = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    int r1 = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    int l2 = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    int r2 = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
};

class ControllerProfileManager {
public:
    bool save(const ControllerProfile& profile,
              const std::filesystem::path& file,
              std::string& error);

    bool load(const std::filesystem::path& file,
              ControllerProfile& profile,
              std::string& error);

private:
    bool validate(const ControllerProfile& profile,
                  std::string& error) const;
};

int16_t scaleAudioSample(int16_t sample, int volume);
void applyAudioVolume(int16_t* samples, size_t sample_count, int volume);
void applyMute(int16_t* samples, size_t sample_count, bool muted);
std::vector<std::string> listAudioDevices();
SDL_AudioDeviceID openAudioDevice(
    const AudioSettings& settings,
    SDL_AudioSpec& desired,
    SDL_AudioSpec& obtained);

void applyControllerProfile(
    const ControllerProfile& profile,
    const SDL_GameController* controller,
    uint16_t& joypad_state);

Settings loadSettings();
GameSettings loadGameSettings(const std::string& game_path);
bool saveGameSettings(const std::string& game_path,
                      const GameSettings& settings);
bool saveSettings(const Settings& settings);
const char* upscalerName(Upscaler mode);
void cycleUpscaler(Settings& settings);
const char* scalingName(ScalingMode mode);
void cycleScaling(Settings& settings);
void changeVolume(Settings& settings, int delta);

inline bool configureVsync(
    const VideoSettings& settings,
    std::string& error) {
    if (SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0) != 0) {
        error = SDL_GetError();
        return false;
    }

    return true;
}

} // namespace emu::frontend