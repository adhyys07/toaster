#define SDL_MAIN_HANDLED
#include "settings.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    const std::string game = "test_game.iso";
    const std::string settings_file = game + ".ini";

    emu::frontend::GameSettings settings;
    settings.scale = 4;
    settings.integer_scale = false;
    settings.bilinear_filter = true;
    settings.vsync = false;
    settings.show_fps = true;
    settings.mute_audio = true;
    settings.fast_forward_speed = 3;

    assert(emu::frontend::saveGameSettings(game, settings));

    const auto loaded = emu::frontend::loadGameSettings(game);

    assert(loaded.scale == 4);
    assert(!loaded.integer_scale);
    assert(loaded.bilinear_filter);
    assert(!loaded.vsync);
    assert(loaded.show_fps);
    assert(loaded.mute_audio);
    assert(loaded.fast_forward_speed == 3);

    // saveGameSettings writes every key, so everything reads back as present.
    assert(loaded.present.scale);
    assert(loaded.present.integer_scale);
    assert(loaded.present.bilinear_filter);
    assert(loaded.present.vsync);
    assert(loaded.present.show_fps);
    assert(loaded.present.mute_audio);
    assert(loaded.present.fast_forward_speed);

    // A file that overrides one key must not claim the others: the frontend
    // uses these flags to decide what may override the global settings.
    {
        std::ofstream partial(settings_file, std::ios::trunc);
        partial << "show_fps=true\n";
    }
    const auto sparse = emu::frontend::loadGameSettings(game);
    assert(sparse.show_fps);
    assert(sparse.present.show_fps);
    assert(!sparse.present.vsync);
    assert(!sparse.present.mute_audio);
    assert(!sparse.present.scale);
    assert(!sparse.present.fast_forward_speed);
    // Unmentioned keys keep their defaults.
    assert(sparse.vsync);
    assert(sparse.fast_forward_speed == 2);

    // No file at all means nothing is present.
    std::filesystem::remove(settings_file);
    const auto absent = emu::frontend::loadGameSettings(game);
    assert(!absent.present.show_fps);
    assert(!absent.present.vsync);

    return 0;
}