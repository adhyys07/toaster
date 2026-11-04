#define SDL_MAIN_HANDLED
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

int g_failures = 0;

// Deliberately not assert(): the default build defines NDEBUG, which would
// compile every check away and leave the test passing without testing.
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    const std::string game = "test_game.iso";
    const std::string settings_file = game + ".ini";

    std::printf("per-game settings\n");

    emu::frontend::GameSettings settings;
    settings.scale = 4;
    settings.integer_scale = false;
    settings.bilinear_filter = true;
    settings.vsync = false;
    settings.show_fps = true;
    settings.mute_audio = true;
    settings.fast_forward_speed = 3;

    check(emu::frontend::saveGameSettings(game, settings),
          "settings are written");

    const auto loaded = emu::frontend::loadGameSettings(game);

    check(loaded.scale == 4, "scale round-trips");
    check(!loaded.integer_scale, "integer_scale round-trips");
    check(loaded.bilinear_filter, "bilinear_filter round-trips");
    check(!loaded.vsync, "vsync round-trips");
    check(loaded.show_fps, "show_fps round-trips");
    check(loaded.mute_audio, "mute_audio round-trips");
    check(loaded.fast_forward_speed == 3, "fast_forward_speed round-trips");

    // saveGameSettings writes every key, so everything reads back as present.
    check(loaded.present.scale && loaded.present.integer_scale &&
              loaded.present.bilinear_filter && loaded.present.vsync &&
              loaded.present.show_fps && loaded.present.mute_audio &&
              loaded.present.fast_forward_speed,
          "a full file marks every key present");

    // A file that overrides one key must not claim the others: the frontend
    // uses these flags to decide what may override the global settings.
    {
        std::ofstream partial(settings_file, std::ios::trunc);
        partial << "show_fps=true\n";
    }
    const auto sparse = emu::frontend::loadGameSettings(game);
    check(sparse.show_fps, "a one-key file applies that key");
    check(sparse.present.show_fps, "the overridden key is marked present");
    check(!sparse.present.vsync && !sparse.present.mute_audio &&
              !sparse.present.scale && !sparse.present.fast_forward_speed,
          "the other keys are not marked present");
    check(sparse.vsync, "an unmentioned key keeps its default");
    check(sparse.fast_forward_speed == 2,
          "an unmentioned numeric key keeps its default");

    // No file at all means nothing is present.
    std::filesystem::remove(settings_file);
    const auto absent = emu::frontend::loadGameSettings(game);
    check(!absent.present.show_fps && !absent.present.vsync,
          "a missing file marks nothing present");

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
