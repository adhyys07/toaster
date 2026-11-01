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

    std::filesystem::remove(settings_file);
    return 0;
}