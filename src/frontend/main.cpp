#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <future>
#include <map>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <set>
#include <memory>
#include <algorithm>
#include <cmath>
#include <SDL_ttf.h>
#include "core/registry.h"
#include "psp_system.h"
#include "ps1_system.h"
#include "gl_fsr_upscaler.h"
#include "ui_font.h"
#include "rewind_buffer.h"
#include "pause_menu.h"
#include "gl_crt_shader.h"
#include <array>
#include "settings.h"
#include "ps2_system.h"
#include "game_metadata.h"
#include "crash_logger.h"
#include "core/core_manager.h"
#include "game_library.h"
#include "artwork_cache.h"
#include "cheat_list.h"
#include "core_manifest.h"
#include "screenshot.h"
#include "bios_manager.h"
#include "sha256.h"
#include "save_state_manager.h"
#include "game_entry.h"
#include "ui/controller_view.h"
#include "ui/layout.h"
#include "ui/library_screen.h"
#include "ui/theme_io.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace {
void printUsage(const char* exe) {
    std::printf(
        "usage: %s [ROM] [options]\n"
        "\n"
        "  --rom PATH          launch a ROM directly\n"
        "  --games-dir PATH    scan this folder in the startup library\n"
        "  --ppsspp-core PATH  path to the PPSSPP Libretro core\n"
        "  --ps1-core PATH     path to the PS1 Libretro core\n"
        "  --ps2 PATH          launch a PS2 image directly\n"
        "  --ps2-core PATH     path to the LRPS2 Libretro core\n"
        "  --core-server URL   HTTPS base URL for core downloads\n"
        "  --core-server-unverified\n"
        "                      install cores even when the server has no\n"
        "                      manifest.txt to check them against\n"
        "  --library-server URL local Python library service URL\n"
        "  --system-dir PATH   system assets/BIOS root (default: system)\n"
        "  --fullscreen        start in fullscreen mode\n"
        "  --mute              mute audio\n"
        "  --verbose           enable diagnostic logging\n"
        "  --help\n"
        "\n"
        "Without a ROM argument, open the game library. O opens a game file.\n"
        "F11 toggles fullscreen; Esc quits during gameplay.\n"
        "F1 opens the pause menu. Shift+F1-F9 save a state, Ctrl+1-9 load\n"
        "one, and holding Backspace rewinds on cores with save states.\n",
        exe);
}

namespace fs = std::filesystem;

// Moved to game_entry.h so the library screen can name it too. The alias
// keeps every use below unqualified.
using GameEntry = emu::frontend::GameEntry;

std::string gamePathKey(const fs::path& path) {
    // Same normalization the library service matching uses, so favorites.txt
    // keys and remote lookups can never disagree about what a path means.
    return emu::frontend::libraryPathKey(path);
}

std::set<std::string> loadFavorites() {
    std::set<std::string> favorites;
    std::ifstream file("favorites.txt");
    std::string path;
    while (std::getline(file, path)) {
        if (!path.empty()) favorites.insert(path);
    }
    return favorites;
}

bool saveFavorites(const std::vector<GameEntry>& games) {
    std::ofstream file("favorites.txt", std::ios::trunc);
    if (!file) return false;

    for (const GameEntry& game : games) {
        if (game.favorite) {
            file << gamePathKey(game.path) << '\n';
        }
    }
    return file.good();
}

GameEntry readGameEntry(const fs::path& rom_path) {
    emu::frontend::GameMetadata meta =
        emu::frontend::detectGameMetadata(rom_path);
    const fs::path sidecar = rom_path.string() + ".meta";
    emu::frontend::loadGameMetadata(sidecar, meta);   // overlays if present

    GameEntry game;
    // Fall back to the detected values: a hand-written sidecar may omit them.
    game.path = meta.path.empty() ? rom_path : meta.path;
    game.title = meta.title.empty() ? rom_path.stem().string() : meta.title;

    // GameMetadata carries only what the frontend needs in order to launch a
    // game. The descriptive fields the library pane shows live in the same
    // sidecar, so read those directly.
    std::ifstream metadata(sidecar);
    std::string line;
    while (std::getline(metadata, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "developer") game.developer = value;
        else if (key == "year") game.year = value;
        else if (key == "genre") game.genre = value;
        else if (key == "description") game.description = value;
    }

    // game.favorite is filled in by scanGames from favorites.txt, which stays
    // the single source of truth for the flag.
    return game;
}

std::vector<GameEntry> scanGames(const fs::path& folder) {
    std::vector<GameEntry> games;
    std::error_code error;
    const std::set<std::string> favorites = loadFavorites();

    if (!fs::exists(folder, error) || error) return games;

    for (const auto& item : fs::recursive_directory_iterator(folder, error)) {
        if (error) break;
        if (!item.is_regular_file()) continue;

        std::string extension = item.path().extension().string();
        for (char& ch : extension) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }

        if (extension == ".ch8" || extension == ".c8" || extension == ".nes" ||
            extension == ".gb" ||
            extension == ".iso" || extension == ".cso" || extension == ".pbp" ||
            extension == ".cue" ||
            extension == ".chd" || extension == ".elf" || extension == ".prx" ||
            extension == ".bin" || extension == ".m3u" || extension == ".ciso" ||
            extension == ".zso" || extension == ".mdf" || extension == ".nrg" ||
            extension == ".dump" || extension == ".img") {
            GameEntry game = readGameEntry(item.path());
            game.favorite =
                favorites.count(gamePathKey(game.path)) != 0;
            if (game.artwork.empty()) {
                game.artwork =
                    emu::frontend::findArtwork(item.path().string());
            }
            games.push_back(std::move(game));
        }
    }

    std::sort(games.begin(), games.end(),
        [](const GameEntry& a, const GameEntry& b) {
            return a.title < b.title;
        });

    return games;
}



void showDebugInfo(const emu::System& system) {
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Debugger",
        system.debugInfo().c_str(),
        nullptr);
}

void drawMenuText(SDL_Renderer* renderer, TTF_Font* font,
                  const std::string& text, int x, int y,
                  SDL_Color color, int wrap_width = 0) {
    SDL_Surface* surface = wrap_width > 0
        ? TTF_RenderUTF8_Blended_Wrapped(
              font, text.c_str(), color, static_cast<Uint32>(wrap_width))
        : TTF_RenderUTF8_Blended(font, text.c_str(), color);

    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect destination{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void fillRoundedRect(SDL_Renderer* renderer, const SDL_Rect& rect,
                     int radius, SDL_Color color) {
    radius = std::max(0, std::min(radius, std::min(rect.w / 2, rect.h / 2)));
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int y = 0; y < rect.h; ++y) {
        const int edge_distance = std::min(y, rect.h - 1 - y);
        int inset = 0;

        if (edge_distance < radius) {
            const double dy = radius - edge_distance;
            inset = radius - static_cast<int>(
                std::sqrt(radius * radius - dy * dy));
        }

        SDL_RenderDrawLine(renderer,
                           rect.x + inset, rect.y + y,
                           rect.x + rect.w - 1 - inset, rect.y + y);
    }
}

void drawMenuPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                   SDL_Color fill, SDL_Color border, SDL_Color accent) {
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);

    SDL_Rect accent_bar{rect.x, rect.y, 4, rect.h};
    SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
    SDL_RenderFillRect(renderer, &accent_bar);
}

bool pointInside(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

void drawMenuButton(SDL_Renderer* renderer, TTF_Font* font,
                    const SDL_Rect& rect, const std::string& label,
                    int mouse_x, int mouse_y) {
    const bool hover = pointInside(rect, mouse_x, mouse_y);

    const SDL_Color fill = hover
        ? SDL_Color{65, 119, 151, 255}
        : SDL_Color{39, 51, 69, 255};
    const SDL_Color border = hover
        ? SDL_Color{111, 190, 226, 255}
        : SDL_Color{69, 85, 106, 255};

    fillRoundedRect(renderer, rect, 9, fill);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);

    int text_width = 0;
    int text_height = 0;
    TTF_SizeUTF8(font, label.c_str(), &text_width, &text_height);

    const SDL_Color text_color{242, 246, 252, 255};
    drawMenuText(renderer, font, label,
                 rect.x + (rect.w - text_width) / 2,
                 rect.y + (rect.h - text_height) / 2,
                 text_color);
}

void showControllerSettingsMenu(emu::frontend::Settings& settings, TTF_Font* font){
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Controller settings",
                                 SDL_GetError(), nullptr);
        return;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    
    const char* names[emu::frontend::kPadActionCount] = {
        "Cross", "Circle", "Square", "Triangle",
        "Start", "Select", "Up", "Down", "Left", "Right", "L1", "R1"
    };
    
    SDL_Window* window = SDL_CreateWindow(
        "Controller Settings", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        620, 560, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return;
    }

    int waiting_for = -1;
    bool running = true;

    while (running) {
        SDL_SetRenderDrawColor(renderer, 17, 22, 28, 255);
        SDL_RenderClear(renderer);
        drawMenuText(renderer, font, "CONTROLLER BUTTONS", 28, 20,
                     SDL_Color{233, 216, 166, 255});
        for (int i = 0; i < emu::frontend::kPadActionCount; ++i) {
            const int y = 68 + i * 36;
            const auto button = static_cast<SDL_GameControllerButton>(
                settings.pad_buttons[i]);
            const char* button_name = SDL_GameControllerGetStringForButton(button);

            drawMenuText(renderer, font, names[i], 38, y,
                         SDL_Color{235, 238, 240, 255});

            const std::string label = waiting_for == i
                ? "Press a controller button..."
                : (button_name ? button_name : "Unassigned");
            
            drawMenuText(renderer, font, label, 300, y,
                         SDL_Color{233, 216, 166, 255});
            const SDL_Scancode key = static_cast<SDL_Scancode>(
                settings.keyboard_keys[i]);
            drawMenuText(renderer, font,
                         SDL_GetScancodeName(key), 470, y,
                         SDL_Color{160, 170, 180, 255});
        }

        drawMenuText(renderer, font, "Click a row, then press a controller button. Esc closes.",
                     28, 518, SDL_Color{160, 170, 180, 255});
        SDL_RenderPresent(renderer);

        SDL_Event event;
        if (!SDL_WaitEvent(&event)) continue;

        if (event.type == SDL_QUIT) {
            running = false;
        } else if (event.type == SDL_KEYDOWN &&
                   event.key.keysym.sym == SDLK_ESCAPE) {
            running = false;
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_LEFT) {
            const int row = (event.button.y - 68) / 36;
            if (event.button.y >= 68 && row >= 0 &&
                row < emu::frontend::kPadActionCount) {
                waiting_for = row;
            }
        } else if (event.type == SDL_KEYDOWN && waiting_for >= 0 &&
                   event.key.keysym.sym != SDLK_ESCAPE) {
            settings.keyboard_keys[waiting_for] = event.key.keysym.scancode;
            waiting_for = -1;
            emu::frontend::saveSettings(settings);
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN && waiting_for >= 0) {
            settings.pad_buttons[waiting_for] = event.cbutton.button;
            waiting_for = -1;
            emu::frontend::saveSettings(settings);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

bool showSettingsMenu(emu::frontend::Settings& settings, TTF_Font* font) {
    SDL_Window* window = SDL_CreateWindow(
        "Settings", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        760, 760, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return false;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return false;
    }

    const emu::frontend::Settings original = settings;
    const SDL_Rect mute_button{410, 90, 270, 44};
    const SDL_Rect fullscreen_button{410, 145, 270, 44};
    const SDL_Rect upscaler_button{410, 200, 270, 44};
    const SDL_Rect sharpness_down{410, 255, 52, 44};
    const SDL_Rect sharpness_up{628, 255, 52, 44};
    const SDL_Rect fps_button{410, 310, 270, 44};
    const SDL_Rect scaling_button{410, 365, 270, 44};
    const SDL_Rect save_button{410, 685, 125, 44};
    const SDL_Rect cancel_button{555, 685, 125, 44};
    const SDL_Rect controller_button{60, 685, 250, 44};
    
    

    bool running = true;
    bool saved = false;
    bool redraw = true;
    while (running) {
        if (redraw) {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            SDL_SetRenderDrawColor(renderer, 17, 22, 28, 255);
            SDL_RenderClear(renderer);

            const SDL_Color gold{233, 216, 166, 255};
            const SDL_Color white{235, 238, 240, 255};
            const SDL_Color muted{160, 170, 180, 255};
            drawMenuText(renderer, font, "SETTINGS", 36, 24, gold);
            drawMenuText(renderer, font, "Mute audio", 60, 98, white);
            drawMenuText(renderer, font, "Start fullscreen", 60, 153, white);
            drawMenuText(renderer, font, "Upscaling filter", 60, 208, white);
            drawMenuText(renderer, font, "FSR sharpness", 60, 263, white);
            drawMenuText(renderer, font, "Show FPS in title", 60, 318, white);
            drawMenuText(renderer, font, "Screen scaling", 60, 373, white);
            drawMenuButton(renderer, font, fps_button,
               settings.show_fps ? "On" : "Off", mouse_x, mouse_y);

            drawMenuButton(renderer, font, mute_button,
                           settings.mute ? "On" : "Off", mouse_x, mouse_y);
            drawMenuButton(renderer, font, fullscreen_button,
                           settings.fullscreen ? "On" : "Off", mouse_x, mouse_y);
            drawMenuButton(renderer, font, upscaler_button,
                           emu::frontend::upscalerName(settings.upscaler),
                           mouse_x, mouse_y);
            drawMenuButton(renderer, font, scaling_button,
                           emu::frontend::scalingName(settings.scaling),
                           mouse_x, mouse_y);
            drawMenuButton(renderer, font, sharpness_down, "-", mouse_x, mouse_y);
            drawMenuButton(renderer, font, sharpness_up, "+", mouse_x, mouse_y);
            drawMenuButton(renderer, font, controller_button,
                           "Configure Controller", mouse_x, mouse_y);

            char sharpness_text[32];
            std::snprintf(sharpness_text, sizeof(sharpness_text), "%.2f",
                          settings.sharpness);
            drawMenuText(renderer, font, sharpness_text, 515, 267, white);
            drawMenuText(renderer, font, "Nearest and Linear use SDL scaling.",
                         36, 480, muted);
            drawMenuText(renderer, font, "FSR 1 uses OpenGL EASU + RCAS passes.",
                         36, 512, muted);
            drawMenuText(renderer, font, "NES mappers 0-4 and 7. Game Boy: MBC1, MBC2, MBC3 and MBC5, no Color.",
                         36, 544, muted, 680);
            drawMenuText(renderer, font, "PICO-8 still requires a separate compatible core.",
                         36, 592, muted, 680);
            drawMenuButton(renderer, font, save_button, "Save", mouse_x, mouse_y);
            drawMenuButton(renderer, font, cancel_button, "Cancel", mouse_x, mouse_y);
            SDL_RenderPresent(renderer);
            redraw = false;
        }

        SDL_Event event;
        if (!SDL_WaitEvent(&event)) continue;
        if (event.type == SDL_QUIT) {
            settings = original;
            running = false;
        } else if (event.type == SDL_MOUSEMOTION) {
            redraw = true;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                settings = original;
                running = false;
            } else if (event.key.keysym.sym == SDLK_RETURN ||
                       event.key.keysym.sym == SDLK_KP_ENTER) {
                saved = emu::frontend::saveSettings(settings);
                running = false;
            }
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_LEFT) {
            if (pointInside(mute_button, event.button.x, event.button.y)) {
                settings.mute = !settings.mute;
                redraw = true;
            } else if (pointInside(fullscreen_button, event.button.x, event.button.y)) {
                settings.fullscreen = !settings.fullscreen;
                redraw = true;
            } else if (pointInside(upscaler_button, event.button.x, event.button.y)) {
                emu::frontend::cycleUpscaler(settings);
                redraw = true;
            } else if (pointInside(sharpness_down, event.button.x, event.button.y)) {
                settings.sharpness = std::max(0.0f, settings.sharpness - 0.05f);
                redraw = true;
            } else if (pointInside(sharpness_up, event.button.x, event.button.y)) {
                settings.sharpness = std::min(1.0f, settings.sharpness + 0.05f);
                redraw = true;
            } else if (pointInside(save_button, event.button.x, event.button.y)) {
                saved = emu::frontend::saveSettings(settings);
                running = false;
            } else if (pointInside(cancel_button, event.button.x, event.button.y)) {
                settings = original;
                running = false;
            } else if (pointInside(controller_button, event.button.x, event.button.y)) {
                showControllerSettingsMenu(settings, font);
                redraw = true;
            } else if (pointInside(fps_button, event.button.x, event.button.y)) {
                settings.show_fps = !settings.show_fps;
                redraw = true;
            } else if (pointInside(scaling_button, event.button.x, event.button.y)) {
                emu::frontend::cycleScaling(settings);
                redraw = true;
            }
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return saved;
}
std::string chooseRomFile();
bool importBios(emu::frontend::BiosSystem target,
                const std::string& system_directory);
bool installCoreFiles(const std::string& server_url, bool verify);

std::string showGameMenu(const fs::path& folder,
                         emu::frontend::Settings& settings,
                         const std::string& system_directory,
                         const std::string& core_server_url,
                         bool core_server_verify,
                         const std::string& library_server_url) {
    auto games = scanGames(folder);
    std::unique_ptr<emu::frontend::GameLibrary> library;

    // Copies the service's cached rows into the list the menu draws. Every
    // refresh goes through here so artwork and tags cannot end up populated
    // on one code path and missing on another.
    const auto adoptLibraryGames = [&] {
        if (!library) return;
        games.clear();
        for (const emu::frontend::RemoteGame& remote : library->games()) {
            GameEntry game;
            game.path = remote.path;
            game.title = remote.title;
            game.favorite = remote.favorite;
            game.artwork = remote.artwork;
            game.tags = remote.tags;
            games.push_back(std::move(game));
        }
    };

    if (!library_server_url.empty()) {
        library = std::make_unique<emu::frontend::GameLibrary>(
            library_server_url);
        std::string library_error;
        if (library->scanFolder(folder.string(), library_error)) {
            adoptLibraryGames();
        } else {
            std::fprintf(stderr, "Library service unavailable: %s\n",
                         library_error.c_str());
            library.reset();
        }
    }

    // A rescan talks to the service over the network, which can take seconds.
    // Running it on the UI thread froze the window, so it runs on a worker and
    // the result is adopted on a later frame.
    //
    // GameLibrary is not thread safe, so while a scan is in flight the UI must
    // not read from it: `scanning` suppresses the statistics line and the
    // actions that look a game up by path.
    std::future<bool> scan_future;
    std::string scan_error;
    bool scanning = false;

    const auto rescanGames = [&] {
        if (!library) {
            games = scanGames(folder);
            return;
        }
        if (scanning) return;   // one at a time

        scanning = true;
        scan_error.clear();
        emu::frontend::GameLibrary* worker = library.get();
        const std::string path = folder.string();
        scan_future = std::async(std::launch::async,
                                 [worker, path, &scan_error] {
            return worker->scanFolder(path, scan_error);
        });
    };

    // Picks up a finished scan. Called once per frame from the UI loop.
    const auto collectScan = [&] {
        if (!scanning || !scan_future.valid()) return false;
        if (scan_future.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            return false;
        }

        const bool ok = scan_future.get();
        scanning = false;
        if (ok) {
            adoptLibraryGames();
        } else {
            std::fprintf(stderr, "Could not rescan library: %s\n",
                         scan_error.c_str());
        }
        return true;
    };
    // Records a launch with the library service. Failures are logged rather
    // than surfaced: the game is already starting and there is nothing the
    // user can do about it.
    const auto markPlayed = [&](const std::string& path) {
        if (!library || scanning) return;
        const emu::frontend::RemoteGame* remote = library->find(path);
        if (!remote) return;
        std::string library_error;
        if (!library->markPlayed(*remote, library_error)) {
            std::fprintf(stderr, "Could not update recent games: %s\n",
                         library_error.c_str());
        }
    };

    // Flipping a favourite finds the game by path. The screen's selection
    // indexes a filtered view, so looking the game up by that index in
    // `games` or in the service's rows would favourite a different one
    // whenever a filter or a search is active.
    //
    // False means the flag did not persist, which the screen reports.
    const auto setFavorite = [&](const std::string& path, bool favorite) {
        const std::string key = gamePathKey(fs::path(path));
        for (GameEntry& game : games) {
            if (gamePathKey(game.path) == key) {
                game.favorite = favorite;
                break;
            }
        }

        if (library && scanning) {
            // Deferring beats racing the worker, which owns the library's
            // rows until it finishes. The local flag above still shows the
            // new state.
            return false;
        }

        if (library) {
            const emu::frontend::RemoteGame* remote = library->find(path);
            if (!remote) {
                std::fprintf(stderr,
                             "The library service does not know %s\n",
                             key.c_str());
                return false;
            }
            std::string library_error;
            if (!library->setFavorite(*remote, favorite, library_error)) {
                std::fprintf(stderr, "Could not set favorite: %s\n",
                             library_error.c_str());
                return false;
            }
            adoptLibraryGames();
            return true;
        }

        return saveFavorites(games);
    };

    // TTF and this font stay up for the settings and controller windows,
    // which still create their own window. The library screen opens its own
    // TextCache and does not use either.
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
        return {};
    }

    std::string font_error;
    TTF_Font* font = emu::frontend::openUiFont(20, font_error);
    if (!font) {
        std::fprintf(stderr, "%s\n", font_error.c_str());
        TTF_Quit();
        return {};
    }

    emu::frontend::ui::LibraryActions actions;
    actions.collectScan = collectScan;
    actions.isScanning = [&] { return scanning; };
    actions.rescan = rescanGames;
    actions.setFavorite = setFavorite;
    actions.markPlayed = markPlayed;
    actions.browseForGame = [] { return chooseRomFile(); };
    actions.themeName = [&] { return settings.theme; };
    actions.setThemeName = [&](const std::string& name) {
        settings.theme = name;
        if (!saveSettings(settings)) {
            std::fprintf(stderr, "Could not save settings.ini\n");
        }
    };
    actions.openSettings = [&] { showSettingsMenu(settings, font); };
    actions.installPs1Bios = [&] {
        importBios(emu::frontend::BiosSystem::Ps1, system_directory);
    };
    actions.installPs2Bios = [&] {
        importBios(emu::frontend::BiosSystem::Ps2, system_directory);
    };
    actions.installCores = [&] {
        installCoreFiles(core_server_url, core_server_verify);
    };
    actions.statusLine = [&]() -> std::string {
        // GameLibrary is not thread safe, so its totals cannot be read while
        // a scan owns them.
        if (!library || scanning) return {};
        const emu::frontend::LibraryStatistics& stats = library->statistics();
        return "Total " + std::to_string(stats.total) + "   Plays " +
               std::to_string(stats.plays);
    };

    const std::string result =
        emu::frontend::ui::runLibraryScreen(games, folder, actions);

    TTF_CloseFont(font);
    TTF_Quit();
    return result;
}

std::string normalizedButtonName(const char* name) {
    std::string result = name ? name : "";
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

bool gamepadButtonDown(SDL_GameController* controller,
                       const char* button_name,
                       bool is_pico8) {
    if (!controller) return false;

    const std::string name = normalizedButtonName(button_name);

    const auto pressed = [controller](SDL_GameControllerButton button) {
        return SDL_GameControllerGetButton(controller, button) != 0;
    };

    const Sint16 left_x =
        SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 left_y =
        SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    constexpr Sint16 deadzone = 16000;
    
    if (name == "A" || (is_pico8 && name == "O"))
        return pressed(SDL_CONTROLLER_BUTTON_A);
    if (name == "B" || (is_pico8 && name == "X"))
        return pressed(SDL_CONTROLLER_BUTTON_B);
    if (name == "SELECT")
        return pressed(SDL_CONTROLLER_BUTTON_BACK);
    if (name == "START" || (is_pico8 && name == "PAUSE"))
        return pressed(SDL_CONTROLLER_BUTTON_START);

    if (name == "UP")
        return pressed(SDL_CONTROLLER_BUTTON_DPAD_UP) || left_y < -deadzone;
    if (name == "DOWN")
        return pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || left_y > deadzone;
    if (name == "LEFT")
        return pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || left_x < -deadzone;
    if (name == "RIGHT")
        return pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || left_x > deadzone;

    return false;
}

int keyboardBindingIndex(const char* button_name) {
    const std::string name = normalizedButtonName(button_name);

    if (name == "A") return emu::frontend::PadCross;
    if (name == "B") return emu::frontend::PadCircle;
    if (name == "X") return emu::frontend::PadSquare;
    if (name == "Y" || name == "SQUARE") return emu::frontend::PadSquare;
    if (name == "O" || name == "TRIANGLE") return emu::frontend::PadTriangle;
    if (name == "START") return emu::frontend::PadStart;
    if (name == "SELECT") return emu::frontend::PadSelect;
    if (name == "UP") return emu::frontend::PadUp;
    if (name == "DOWN") return emu::frontend::PadDown;
    if (name == "LEFT") return emu::frontend::PadLeft;
    if (name == "RIGHT") return emu::frontend::PadRight;
    if (name == "L1") return emu::frontend::PadL1;
    if (name == "R1") return emu::frontend::PadR1;
    return -1;
}

SDL_Scancode nesPlayerTwoKey(const char* button_name) {
    const std::string name = normalizedButtonName(button_name);

    if (name == "UP") return SDL_SCANCODE_W;
    if (name == "DOWN") return SDL_SCANCODE_S;
    if (name == "LEFT") return SDL_SCANCODE_A;
    if (name == "RIGHT") return SDL_SCANCODE_D;
    if (name == "A") return SDL_SCANCODE_K;
    if (name == "B") return SDL_SCANCODE_J;
    if (name == "START") return SDL_SCANCODE_I;
    if (name == "SELECT") return SDL_SCANCODE_U;

    return SDL_SCANCODE_UNKNOWN;
}

// Save-state slots live in a "states" folder beside the executable so a
// portable install carries them with the binary.
emu::frontend::SaveStateManager& saveStateManager() {
    static emu::frontend::SaveStateManager manager([] {
        char* raw_base = SDL_GetBasePath();
        const fs::path base = raw_base ? fs::path(raw_base) : fs::current_path();
        if (raw_base) SDL_free(raw_base);
        return base / "states";
    }());
    return manager;
}

bool saveStateToSlot(const emu::System& system,
                     const std::string& game_path,
                     int slot) {
    std::vector<uint8_t> state;
    if (!system.saveState(state) || state.empty()) return false;

    const std::string blob(reinterpret_cast<const char*>(state.data()),
                           state.size());
    std::string error;
    // Key on the normalized path so launching the same ROM through a relative
    // path and an absolute one reaches the same slots.
    if (!saveStateManager().save(slot, gamePathKey(game_path), blob, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    return true;
}

bool loadStateFromSlot(emu::System& system,
                       const std::string& game_path,
                       int slot) {
    std::string blob;
    std::string error;
    if (!saveStateManager().load(slot, gamePathKey(game_path), blob, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    const std::vector<uint8_t> state(blob.begin(), blob.end());
    return system.loadState(state);
}

int runGenericGame(const std::string& rom_path,
                   bool mute,
                   bool start_fullscreen,
                   emu::frontend::Settings& settings) {
    std::string error;

    // Per-game overrides from "<rom>.ini". Only keys the file actually
    // contained are applied: loadGameSettings fills the rest with struct
    // defaults, so copying those over would reset the global settings on
    // every launch of a game that has no override for them.
    const emu::frontend::GameSettings game_settings =
        emu::frontend::loadGameSettings(rom_path);
    if (game_settings.present.vsync) settings.vsync = game_settings.vsync;
    if (game_settings.present.show_fps)
        settings.show_fps = game_settings.show_fps;
    if (game_settings.present.mute_audio)
        settings.mute = game_settings.mute_audio;
    if (game_settings.present.integer_scale) {
        settings.scaling = game_settings.integer_scale
            ? emu::frontend::ScalingMode::Integer
            : emu::frontend::ScalingMode::Aspect;
    }
    if (game_settings.present.bilinear_filter &&
        game_settings.bilinear_filter &&
        settings.upscaler == emu::frontend::Upscaler::Nearest) {
        settings.upscaler = emu::frontend::Upscaler::Linear;
    }

    emu::SystemPtr system = emu::createForFile(rom_path, error);
    
    if (!system) {
        std::fprintf(stderr, "Could not choose emulator core: %s\n", error.c_str());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!system->loadRom(rom_path, error)) {
        std::fprintf(stderr, "Could not load ROM: %s\n", error.c_str());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    const emu::VideoFrame initial = system->video();
    if (!initial.pixels || initial.width <= 0 || initial.height <= 0) {
        std::fprintf(stderr, "%s returned an invalid video frame\n", system->name());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    const std::string base_title = std::string(system->name()) + " - " + rom_path;
    std::string title = base_title;

    // Cheats come from "<game>.cht" beside the ROM. They stay loaded so the
    // F9 toggle can turn them on and off without re-reading the file.
    const std::vector<emu::frontend::Cheat> cheats =
        system->supportsPatches() ? emu::frontend::loadCheatFile(rom_path)
                                  : std::vector<emu::frontend::Cheat>{};
    bool cheats_enabled = !cheats.empty();
    const auto pushCheats = [&] {
        std::vector<emu::MemoryPatch> patches;
        if (cheats_enabled) {
            for (const emu::frontend::Cheat& cheat : cheats) {
                if (!cheat.enabled) continue;
                emu::MemoryPatch patch;
                patch.address = cheat.address;
                patch.value = cheat.value;
                patch.has_compare = cheat.compare.has_value();
                patch.compare = cheat.compare.value_or(0);
                patches.push_back(patch);
            }
        }
        system->setPatches(patches);
    };
    if (!cheats.empty()) {
        pushCheats();
        std::printf("Loaded %zu cheat%s from %s\n", cheats.size(),
                    cheats.size() == 1 ? "" : "s",
                    fs::path(rom_path).replace_extension(".cht")
                        .filename().string().c_str());
    }

    emu::frontend::RewindBuffer rewind_buffer(
        settings.rewind_seconds * 30, 2);
    bool rewinding = false;

    // The pause menu only needs a font once it is opened, and opening one
    // costs an SDL_ttf init, so both are deferred until then. A missing font
    // is not fatal: the selected entry is always mirrored in the window title,
    // which is also the only way to show it in the OpenGL render paths.
    emu::frontend::PauseMenu menu;
    TTF_Font* menu_font = nullptr;
    bool menu_font_attempted = false;
    bool ttf_initialized_here = false;
    const auto menuFont = [&]() -> TTF_Font* {
        if (menu_font || menu_font_attempted) return menu_font;
        menu_font_attempted = true;
        if (TTF_WasInit() == 0) {
            if (TTF_Init() != 0) {
                std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
                return nullptr;
            }
            ttf_initialized_here = true;
        }
        std::string font_error;
        menu_font = emu::frontend::openUiFont(20, font_error);
        if (!menu_font) std::fprintf(stderr, "%s\n", font_error.c_str());
        return menu_font;
    };

    const bool rewind_supported = [&] {
        std::vector<uint8_t> probe;
        return system->saveState(probe) && !probe.empty();
    }();

    const bool use_fsr = settings.upscaler == emu::frontend::Upscaler::Fsr1;
    const bool use_crt = settings.upscaler == emu::frontend::Upscaler::Crt;
    // Both post-process filters render through the same GL context.
    const bool use_opengl = use_fsr || use_crt;
    if (use_opengl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (use_opengl) window_flags |= SDL_WINDOW_OPENGL;
    if (start_fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    SDL_Window* window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 720,
        window_flags);

    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = nullptr;
    SDL_Renderer* renderer = nullptr;
    emu::frontend::FsrUpscaler fsr;
    emu::frontend::CrtShader crt;
    if (use_opengl) {
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context) {
            std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return EXIT_FAILURE;
        }

        char* base_path = SDL_GetBasePath();
        fs::path shader_directory = "assets/shaders";
        if (base_path) {
            const fs::path executable_shader_directory =
                fs::path(base_path) / "assets" / "shaders";
            const char* probe = use_crt ? "crt.frag" : "fsr_easu.frag";
            if (fs::exists(executable_shader_directory / probe)) {
                shader_directory = executable_shader_directory;
            }
            SDL_free(base_path);
        }

        const bool initialized = use_crt
            ? crt.initialize(window, shader_directory.string(), error)
            : fsr.initialize(window, shader_directory.string(), error);
        if (!initialized) {
            std::fprintf(stderr, "%s initialization failed: %s\n",
                         use_crt ? "CRT filter" : "FSR", error.c_str());
            crt.shutdown();
            fsr.shutdown();
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return EXIT_FAILURE;
        }
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    settings.upscaler == emu::frontend::Upscaler::Linear ? "1" : "0");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return EXIT_FAILURE;
        }
    }

    // The input display needs the UI layer's painter and font cache, which
    // cost four font opens, so they are built only if it is actually on. It
    // also only works on the SDL renderer paths: FSR and CRT present through
    // OpenGL and never touch this renderer.
    std::unique_ptr<emu::frontend::ui::Painter> hud_painter;
    std::unique_ptr<emu::frontend::ui::TextCache> hud_text;
    emu::frontend::ui::Theme hud_theme;
    emu::frontend::ui::Metrics hud_metrics;
    bool hud_attempted = false;

    const auto inputDisplayReady = [&] {
        if (!settings.input_display || !renderer) return false;
        if (hud_attempted) return hud_text != nullptr;
        hud_attempted = true;

        if (TTF_WasInit() == 0) {
            if (TTF_Init() != 0) {
                std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
                return false;
            }
            ttf_initialized_here = true;
        }

        hud_metrics.scale =
            emu::frontend::ui::displayScale(window, renderer);
        hud_theme = emu::frontend::ui::repairTheme(
            emu::frontend::ui::themePreset(
                emu::frontend::ui::themeFromName(settings.theme)));

        auto cache = std::make_unique<emu::frontend::ui::TextCache>();
        std::string hud_error;
        if (!cache->open(hud_metrics.scale, hud_error)) {
            std::fprintf(stderr, "Input display unavailable: %s\n",
                         hud_error.c_str());
            return false;
        }
        hud_text = std::move(cache);
        hud_painter =
            std::make_unique<emu::frontend::ui::Painter>(renderer);
        return true;
    };

    int texture_width = 0;
    int texture_height = 0;
    SDL_Texture* texture = nullptr;

    auto updateTexture = [&]() -> bool {
        const emu::VideoFrame frame = system->video();
        if (!frame.pixels || frame.width <= 0 || frame.height <= 0) return false;

        if (!texture || frame.width != texture_width || frame.height != texture_height) {
            if (texture) SDL_DestroyTexture(texture);
            texture_width = frame.width;
            texture_height = frame.height;
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        texture_width, texture_height);
            if (!texture) return false;
        }

        return SDL_UpdateTexture(texture, nullptr, frame.pixels,
                                 texture_width * static_cast<int>(sizeof(uint32_t))) == 0;
    };

    if (!use_opengl && !updateTexture()) {
        std::fprintf(stderr, "Could not create the game's video texture: %s\n", SDL_GetError());
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_AudioDeviceID audio = 0;
    if (!mute && system->sampleRate() > 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec wanted{};
        wanted.freq = system->sampleRate();
        wanted.format = AUDIO_F32SYS;
        wanted.channels = 1;
        wanted.samples = static_cast<Uint16>(std::clamp(settings.audio_buffer_samples, 256, 4096));
        audio = SDL_OpenAudioDevice(
            settings.audio_device.empty() ? nullptr : settings.audio_device.c_str(),
            0, &wanted, nullptr, 0);
        if (audio) {
            SDL_PauseAudioDevice(audio, 0);
        } else {
            std::fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
        }
    }

    std::printf("Running %s: %s\n", system->name(), rom_path.c_str());
    std::printf("Controls use the default keys listed in the menu bindings. Esc quits.\n");

    bool running = true;
    std::vector<float> audio_buffer(4096);
    const double refresh = system->refreshRate() > 0.0 ? system->refreshRate() : 60.0;
    const Uint64 performance_frequency = SDL_GetPerformanceFrequency();
    const double target_frame_ticks = static_cast<double>(performance_frequency) / refresh;
    double next_frame = static_cast<double>(SDL_GetPerformanceCounter());
    Uint64 fps_window_start = SDL_GetPerformanceCounter();
    unsigned fps_frame_count = 0;
    bool paused = false;
    bool frame_advance = false;
    bool fast_forward = false;

    const auto syncMenuTitle = [&] {
        SDL_SetWindowTitle(window,
                           menu.isOpen()
                               ? (base_title + " | PAUSED | " +
                                  menu.selectedLabel()).c_str()
                               : base_title.c_str());
    };

    const auto toggleFullscreen = [&] {
        const bool is_fullscreen =
            (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        if (SDL_SetWindowFullscreen(
                window, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            std::fprintf(stderr, "Could not toggle fullscreen: %s\n",
                         SDL_GetError());
        }
    };

    // Slot 1 is what the menu reaches; F-keys still address all nine slots.
    const auto activateMenuItem = [&] {
        using emu::frontend::PauseAction;
        switch (menu.selectedAction()) {
        case PauseAction::Resume:
            menu.close();
            break;
        case PauseAction::SaveState:
            if (!saveStateToSlot(*system, rom_path, 1))
                std::fprintf(stderr, "Save state slot 1 failed\n");
            break;
        case PauseAction::LoadState:
            if (loadStateFromSlot(*system, rom_path, 1))
                rewind_buffer.clear();
            else
                std::fprintf(stderr, "Load state slot 1 failed\n");
            break;
        case PauseAction::ResetGame:
            system->reset();
            rewind_buffer.clear();
            menu.close();
            break;
        case PauseAction::ToggleVsync:
            settings.vsync = !settings.vsync;
            if (renderer) SDL_RenderSetVSync(renderer, settings.vsync ? 1 : 0);
            else SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
            break;
        case PauseAction::ToggleFps:
            settings.show_fps = !settings.show_fps;
            break;
        case PauseAction::DecreaseVolume:
            emu::frontend::changeVolume(settings, -5);
            break;
        case PauseAction::IncreaseVolume:
            emu::frontend::changeVolume(settings, 5);
            break;
        case PauseAction::ToggleMute:
            settings.mute = !settings.mute;
            break;
        case PauseAction::ToggleFullscreen:
            toggleFullscreen();
            break;
        case PauseAction::Quit:
            running = false;
            break;
        }
        paused = menu.isOpen();
        syncMenuTitle();
    };

    std::array<SDL_GameController*, 2> gamepads{};
    const bool gamecontroller_ready =
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;

    auto openGamepad = [&](int device_index) {
        if (!gamecontroller_ready || !SDL_IsGameController(device_index)) {
            return;
        }

        int slot = -1;
        for (int i = 0; i < static_cast<int>(gamepads.size()); ++i) {
            if (!gamepads[i]) {
                slot = i;
                break;
            }
        }

        if (slot < 0) return;

        gamepads[slot] = SDL_GameControllerOpen(device_index);
        if (gamepads[slot]) {
            const char* name = SDL_GameControllerName(gamepads[slot]);
            std::printf("Player %d controller: %s\n",
                    slot + 1, name ? name : "unknown");
        }
    };

    if (gamecontroller_ready) {
        SDL_GameControllerEventState(SDL_ENABLE);
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            openGamepad(i);
        }
    } else {
        std::fprintf(stderr, "Controller support unavailable: %s\n", SDL_GetError());
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && menu.isOpen()) {
                // While the menu is up it owns the keyboard, so the arrow keys
                // navigate instead of also reaching the game, and Escape backs
                // out of the menu rather than quitting.
                const SDL_Scancode scancode = event.key.keysym.scancode;
                if (scancode == SDL_SCANCODE_UP) {
                    menu.moveUp();
                    syncMenuTitle();
                } else if (scancode == SDL_SCANCODE_DOWN) {
                    menu.moveDown();
                    syncMenuTitle();
                } else if (scancode == SDL_SCANCODE_RETURN ||
                           scancode == SDL_SCANCODE_KP_ENTER) {
                    if (!event.key.repeat) activateMenuItem();
                } else if (scancode == SDL_SCANCODE_ESCAPE ||
                           scancode == SDL_SCANCODE_F1) {
                    menu.close();
                    paused = false;
                    syncMenuTitle();
                }
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       event.key.keysym.scancode == SDL_SCANCODE_F1 &&
                       (event.key.keysym.mod & KMOD_SHIFT) == 0) {
                menu.open();
                paused = true;
                menuFont();
                syncMenuTitle();
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       event.key.keysym.scancode == SDL_SCANCODE_F11) {
                toggleFullscreen();
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_p) {
                    paused = !paused;
                } else if (key == SDLK_n && paused) {
                    frame_advance = true;
                } else if (key == SDLK_TAB) {
                    fast_forward = !fast_forward;
                } else if (key == SDLK_F9 && !cheats.empty()) {
                    cheats_enabled = !cheats_enabled;
                    pushCheats();
                    std::printf("Cheats %s\n",
                                cheats_enabled ? "enabled" : "disabled");
                } else if (key == SDLK_F10) {
                    std::string shot_error;
                    const std::string path = emu::frontend::saveScreenshot(
                        system->video(), rom_path, shot_error);
                    if (path.empty()) {
                        std::fprintf(stderr, "Screenshot failed: %s\n",
                                     shot_error.c_str());
                    } else {
                        std::printf("Saved %s\n", path.c_str());
                    }
                } else if (key == SDLK_F12) {
                    showDebugInfo(*system);
                } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                    settings.volume = std::max(0, settings.volume - 5);
                } else if (key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
                    settings.volume = std::min(100, settings.volume + 5);
                } else if (key == SDLK_m) {
                    settings.mute = !settings.mute;
                } else if (key >= SDLK_F1 && key <= SDLK_F9 &&
                           (event.key.keysym.mod & KMOD_SHIFT)) {
                    // Plain F1 opens the pause menu, so saving is on
                    // Shift+F1-F9 for every slot rather than only for slot 1.
                    const int slot = static_cast<int>(key - SDLK_F1) + 1;
                    if (!saveStateToSlot(*system, rom_path, slot)) {
                        std::fprintf(stderr,
                                     "Save state slot %d is unsupported or failed\n",
                                     slot);
                    }
                } else if (key >= SDLK_1 && key <= SDLK_9 &&
                           (event.key.keysym.mod & KMOD_CTRL)) {
                    const int slot = static_cast<int>(key - SDLK_1) + 1;
                    if (loadStateFromSlot(*system, rom_path, slot)) {
                        // History captured before the jump no longer belongs
                        // to this timeline.
                        rewind_buffer.clear();
                    } else {
                        std::fprintf(stderr,
                                     "Load state slot %d is unsupported or failed\n",
                                     slot);
                    }
                }
            } else if (event.type == SDL_CONTROLLERBUTTONDOWN && menu.isOpen()) {
                const Uint8 button = event.cbutton.button;
                if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                    menu.moveUp();
                    syncMenuTitle();
                } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                    menu.moveDown();
                    syncMenuTitle();
                } else if (button == SDL_CONTROLLER_BUTTON_A) {
                    activateMenuItem();
                } else if (button == SDL_CONTROLLER_BUTTON_B) {
                    menu.close();
                    paused = false;
                    syncMenuTitle();
                }
            } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                // Added events carry a device index.
                openGamepad(event.cdevice.which);
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                // Removed events carry the joystick instance ID.
                for (SDL_GameController*& gamepad : gamepads) {
                    if (!gamepad) continue;

                    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(gamepad);
                    if (SDL_JoystickInstanceID(joystick) == event.cdevice.which) {
                        SDL_GameControllerClose(gamepad);
                        gamepad = nullptr;
                        break;
                    }
                }
            }

        }

        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        // The menu owns the input while it is up: held keys must not reach the
        // game, and Backspace must navigate rather than rewind.
        rewinding = rewind_supported && !menu.isOpen() &&
                    keyboard[SDL_SCANCODE_BACKSPACE];
        const std::string system_name = normalizedButtonName(system->name());
        const bool is_pico8 = system_name == "PICO-8" || system_name == "PICO8";

        for (int port = 0; port < system->portCount(); ++port) {
            uint32_t button_state = 0;

            for (const emu::ButtonInfo& button : system->buttons()) {
                bool down = false;
                const std::string button_name = normalizedButtonName(button.name);

                // Player 1 uses named bindings so core button ordering does
                // not change the meaning of Start and Select.
                if (port == 0) {
                    const int binding_index = keyboardBindingIndex(button.name);
                    const SDL_Scancode key = binding_index >= 0
                        ? static_cast<SDL_Scancode>(
                            settings.keyboard_keys[binding_index])
                        : SDL_GetScancodeFromName(button.default_key);
                    if (key != SDL_SCANCODE_UNKNOWN && keyboard[key]) {
                        down = true;
                    }
                }

                // Common PICO-8 keyboard aliases for its two action buttons.
                if (port == 0 && is_pico8) {
                    if (button_name == "O") {
                        down = down || keyboard[SDL_SCANCODE_Z] ||
                               keyboard[SDL_SCANCODE_C];
                    } else if (button_name == "X") {
                        down = down || keyboard[SDL_SCANCODE_X] ||
                               keyboard[SDL_SCANCODE_V];
                    }
                }

                // Player 2 keyboard bindings for NES.
                if (port == 1 && system_name == "NES") {
                    const SDL_Scancode key = nesPlayerTwoKey(button.name);
                    if (key != SDL_SCANCODE_UNKNOWN && keyboard[key]) down = true;
                }

                // Controller slot 0 drives port 0; slot 1 drives port 1.
                if (port < static_cast<int>(gamepads.size()) && gamepads[port] &&
                    gamepadButtonDown(gamepads[port], button.name, is_pico8)) {
                    down = true;
                }
                if (down) button_state |= button.mask;
            }

            system->setInput(port, menu.isOpen() ? 0u : button_state);
        }

        if (rewinding) {
            // Walk back through captured states; stop rewinding once the
            // history runs dry so the game resumes instead of freezing.
            if (!rewind_buffer.rewind(*system)) rewinding = false;
        } else if (!paused || frame_advance) {
            // Capturing costs a full serialize, so skip it entirely for cores
            // without save states and when the user has turned rewind off.
            if (rewind_supported && settings.rewind_seconds > 0)
                rewind_buffer.capture(*system);
            system->runFrame();
            frame_advance = false;
        }
        if (use_opengl) {
            const bool rendered = use_crt
                ? crt.render(system->video(), system->aspectRatio(),
                             settings.sharpness, error)
                : fsr.render(system->video(), system->aspectRatio(),
                             settings.sharpness, error);
            if (!rendered) {
                std::fprintf(stderr, "%s render failed: %s\n",
                             use_crt ? "CRT filter" : "FSR", error.c_str());
                break;
            }
            SDL_GL_SwapWindow(window);
        } else {
            if (!updateTexture()) {
                std::fprintf(stderr, "Could not update the game image: %s\n", SDL_GetError());
                break;
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            int window_width = 0;
            int window_height = 0;
            SDL_GetWindowSize(window, &window_width, &window_height);
            const emu::VideoFrame frame = system->video();
            const SDL_Rect viewport = emu::frontend::calculateViewport(
                window_width, window_height, frame.width, frame.height,
                system->aspectRatio(), settings.scaling);
            SDL_RenderCopy(renderer, texture, nullptr, &viewport);

            if (menu.isOpen()) {
                if (TTF_Font* font = menuFont()) {
                    constexpr int kRowHeight = 32;
                    const int rows = static_cast<int>(menu.itemCount());
                    const SDL_Rect panel{
                        (window_width - 320) / 2,
                        (window_height - (rows * kRowHeight + 32)) / 2,
                        320, rows * kRowHeight + 32};

                    drawMenuPanel(renderer, panel, SDL_Color{16, 18, 26, 240},
                                  SDL_Color{60, 68, 86, 255},
                                  SDL_Color{70, 130, 220, 255});

                    for (int row = 0; row < rows; ++row) {
                        const bool is_selected =
                            static_cast<std::size_t>(row) == menu.selectedIndex();
                        const SDL_Rect line{panel.x + 10,
                                            panel.y + 16 + row * kRowHeight,
                                            panel.w - 20, kRowHeight};
                        if (is_selected) {
                            SDL_SetRenderDrawColor(renderer, 30, 88, 178, 255);
                            SDL_RenderFillRect(renderer, &line);
                        }
                        drawMenuText(renderer, font,
                                     menu.itemLabel(
                                         static_cast<std::size_t>(row)),
                                     line.x + 12, line.y + 5,
                                     is_selected
                                         ? SDL_Color{255, 255, 255, 255}
                                         : SDL_Color{188, 196, 212, 255});
                    }
                }
            }

            // Read straight from the pad rather than from the joypad
            // state the core sees, so the display shows what the hardware is
            // doing even when a binding is wrong.
            if (inputDisplayReady()) {
                emu::frontend::ui::drawInputDisplay(
                    renderer, *hud_painter, *hud_text, hud_theme, hud_metrics,
                    SDL_Rect{0, 0, window_width, window_height},
                    emu::frontend::ui::readController(gamepads[0]), 255);
            }

            SDL_RenderPresent(renderer);
        }

        // The menu keeps the window title, so the FPS counter must not
        // overwrite it while the menu is up.
        if (settings.show_fps && !menu.isOpen()) {
            ++fps_frame_count;
            const Uint64 fps_now = SDL_GetPerformanceCounter();
            const double elapsed = static_cast<double>(fps_now - fps_window_start) /
                                   static_cast<double>(performance_frequency);
            if (elapsed >= 1.0) {
                char fps_title[512];
                std::snprintf(fps_title, sizeof(fps_title), "%s | %.1f FPS",
                              base_title.c_str(), fps_frame_count / elapsed);
                SDL_SetWindowTitle(window, fps_title);
                fps_window_start = fps_now;
                fps_frame_count = 0;
            }
        }

        if (audio) {
            const size_t count = system->readAudio(audio_buffer.data(), audio_buffer.size());
            if (count > 0) {
                const float gain = settings.mute
                    ? 0.0f
                    : static_cast<float>(std::clamp(settings.volume, 0, 100)) / 100.0f;
                for (size_t i = 0; i < count; ++i) {
                    audio_buffer[i] *= gain;
                }
                if (SDL_GetQueuedAudioSize(audio) >
                    static_cast<Uint32>(system->sampleRate() * sizeof(float) / 4)) {
                    SDL_ClearQueuedAudio(audio);
                }
                SDL_QueueAudio(audio, audio_buffer.data(),
                               static_cast<Uint32>(count * sizeof(float)));
            }
        }

        const double speed_multiplier = fast_forward ? std::max(1, game_settings.fast_forward_speed) : 1;
        next_frame += target_frame_ticks / speed_multiplier;
        const double now = static_cast<double>(SDL_GetPerformanceCounter());
        if (next_frame > now) {
            const Uint32 delay_ms = static_cast<Uint32>(
                (next_frame - now) * 1000.0 / performance_frequency);
            if (delay_ms > 0) SDL_Delay(delay_ms);
        } else {
            next_frame = now;
        }
    }

    // Both hold textures from `renderer`, so they go before it does.
    hud_painter.reset();
    hud_text.reset();

    for (SDL_GameController* gamepad : gamepads) {
        if (gamepad) SDL_GameControllerClose(gamepad);
    }
    if (menu_font) TTF_CloseFont(menu_font);
    // Only tear SDL_ttf down if the pause menu is what brought it up.
    if (ttf_initialized_here) TTF_Quit();
    if (audio) SDL_CloseAudioDevice(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (gl_context) {
        crt.shutdown();
        fsr.shutdown();
        SDL_GL_DeleteContext(gl_context);
    }
    SDL_DestroyWindow(window);
    if (!emu::frontend::saveSettings(settings)) {
        std::fprintf(stderr, "Could not save settings.ini\n");
    }
    SDL_Quit();
    return EXIT_SUCCESS;
}

std::string chooseRomFile() {
#ifdef _WIN32
    char filename[32768] = {};

    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter =
        "Supported games\0*.nes;*.gb;*.ch8;*.c8;*.cue;*.iso;*.cso;*.pbp;*.chd;*.elf;*.prx;*.bin;*.m3u;*.ciso;*.zso;*.mdf;*.nrg;*.dump;*.img\0"
        "PSP games\0*.iso;*.cso;*.pbp;*.chd;*.elf;*.prx\0"
        "PS2 games\0*.iso;*.cso;*.chd;*.elf;*.bin;*.m3u;*.ciso;*.zso;*.mdf;*.nrg;*.dump;*.img\0"
        "All files\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = "Open a game ROM";

    if (GetOpenFileNameA(&dialog)) {
        return filename;
    }
#else
    std::fprintf(stderr, "The ROM picker is currently implemented for Windows only.\n");
#endif
    return {};
}

// Resolves the --system-dir value the way the rest of the frontend expects: a
// relative path is tried as given, then beside the executable, then one level
// up, so a portable install and a build tree both work.
fs::path resolveSystemRoot(const std::string& system_directory) {
    fs::path base_path;
    if (char* raw_base = SDL_GetBasePath()) {
        base_path = raw_base;
        SDL_free(raw_base);
    }

    const fs::path requested(system_directory);
    std::vector<fs::path> candidates{requested};
    if (!requested.is_absolute() && !base_path.empty()) {
        candidates.push_back(base_path / requested);
        candidates.push_back(base_path.parent_path() / requested);
    }

    std::error_code error;
    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::is_directory(candidate, error) && !error) return candidate;
    }

    if (requested.is_absolute()) return requested;
    return (base_path.empty() ? fs::current_path() : base_path) / requested;
}

// Opens the platform file picker filtered to BIOS dumps.
std::string chooseBiosFile(const char* title) {
#ifdef _WIN32
    char filename[32768] = {};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter =
        "BIOS images (*.bin;*.rom;*.bios)\0*.bin;*.rom;*.bios\0"
        "All files\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = title;

    if (GetOpenFileNameA(&dialog)) return filename;
#else
    (void)title;
    std::fprintf(stderr,
                 "The BIOS picker is currently implemented for Windows only.\n");
#endif
    return {};
}

bool importBios(emu::frontend::BiosSystem target,
                const std::string& system_directory) {
    const char* const label =
        target == emu::frontend::BiosSystem::Ps1 ? "PS1 BIOS" : "PS2 BIOS";

#ifndef _WIN32
    (void)system_directory;
    const char* const folder =
        target == emu::frontend::BiosSystem::Ps1
            ? "system/swanstation/bios"
            : "system/pcsx2/bios";
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION, label,
        (std::string("Copy your legally dumped BIOS into ") + folder + ".")
            .c_str(),
        nullptr);
    return false;
#else
    const std::string source = chooseBiosFile(
        target == emu::frontend::BiosSystem::Ps1
            ? "Select a PS1 BIOS dump you own"
            : "Select a PS2 BIOS dump you own");
    if (source.empty()) return false;

    emu::frontend::BiosManager manager(resolveSystemRoot(system_directory));
    const emu::frontend::BiosStatus before = manager.check(target);

    std::string error;
    if (!manager.import(target, source, error)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, label, error.c_str(),
                                 nullptr);
        return false;
    }

    const emu::frontend::BiosStatus after = manager.check(target);
    const std::string message =
        (before.installed && before.path == after.path
             ? std::string("That BIOS is already installed:\n")
             : std::string("BIOS installed to:\n")) +
        after.path.string() +
        "\n\nRestart the game if it is already running.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, label,
                             message.c_str(), nullptr);
    return true;
#endif
}

#ifdef _WIN32
bool downloadTo(const std::string& url, const fs::path& destination,
                std::string& error) {
    using UrlDownloadToFileFn = HRESULT (*)(
        void*, const char*, const char*, unsigned long, void*);
    HMODULE urlmon = LoadLibraryA("urlmon.dll");
    const auto download = urlmon
        ? reinterpret_cast<UrlDownloadToFileFn>(
            reinterpret_cast<void*>(
                GetProcAddress(urlmon, "URLDownloadToFileA")))
        : nullptr;

    if (!download) {
        if (urlmon) FreeLibrary(urlmon);
        error = "Could not load urlmon.dll to perform the download.";
        return false;
    }

    const HRESULT result =
        download(nullptr, url.c_str(), destination.string().c_str(), 0, nullptr);
    FreeLibrary(urlmon);

    std::error_code filesystem_error;
    if (SUCCEEDED(result) &&
        fs::is_regular_file(destination, filesystem_error) &&
        !filesystem_error) {
        return true;
    }

    std::error_code cleanup;
    fs::remove(destination, cleanup);

    switch (static_cast<unsigned long>(result)) {
    case 0x800C0005ul:  // INET_E_RESOURCE_NOT_FOUND
        error = "The server returned 404 (file not found).";
        break;
    case 0x800C0002ul:  // INET_E_INVALID_URL
        error = "The URL is not valid.";
        break;
    case 0x800C000Dul:  // INET_E_UNKNOWN_PROTOCOL
        error = "The URL uses a protocol Windows cannot fetch.";
        break;
    case 0x800C0019ul:  // INET_E_SECURITY_PROBLEM
        error = "The server's certificate was rejected.";
        break;
    case 0x800C0008ul:  // INET_E_DOWNLOAD_FAILURE
        error = "The download failed or the connection was lost.";
        break;
    default: {
        char text[64];
        std::snprintf(text, sizeof(text),
                      "The download failed (HRESULT 0x%08lX).",
                      static_cast<unsigned long>(result));
        error = text;
        break;
    }
    }
    return false;
}
#endif


bool installCoreFiles(const std::string& server_url, bool verify) {
#ifdef _WIN32
    char* raw_base = SDL_GetBasePath();
    const fs::path base = raw_base ? fs::path(raw_base) : fs::current_path();
    if (raw_base) SDL_free(raw_base);

    const fs::path core_directory = base / "cores";
    std::error_code error;
    fs::create_directories(core_directory, error);
    if (error) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Core installation",
            ("Could not create the cores folder:\n" +
             core_directory.string() + "\n\n" + error.message()).c_str(),
            nullptr);
        return false;
    }

    std::string base_url = server_url;
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
    if (base_url.rfind("https://", 0) != 0) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Core installation",
            "The core server URL must use HTTPS.", nullptr);
        return false;
    }

    const std::vector<std::string> files = {
        "swanstation_libretro.dll",
        "ppsspp_libretro.dll",
        "pcsx2_libretro.dll"
    };

    // Fetch and parse the manifest before touching any core file.
    const fs::path manifest_temp = core_directory / "manifest.txt.download";
    const std::string manifest_url = base_url + "/manifest.txt";
    bool verified = true;

    std::string download_error;
    if (!downloadTo(manifest_url, manifest_temp, download_error)) {
        if (verify) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Core installation",
                ("Could not fetch the core manifest.\n\n" + manifest_url +
                 "\n" + download_error +
                 "\n\nCores are loaded as native code, so Toaster will not "
                 "install any it cannot verify. The manifest lists one line "
                 "per file:\n\n    <sha256>  <filename>\n\nGenerate it on the "
                 "server with:\n    sha256sum -t *.dll > manifest.txt")
                    .c_str(),
                nullptr);
            return false;
        }

        // --core-server-unverified was passed. Skipping the check means
        // trusting the server and the network to hand over native code that
        // this process will load, so it is confirmed rather than assumed.
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Install anyway"},
        };
        const std::string message =
            "No manifest is available, so the downloaded cores cannot be "
            "verified.\n\n" + manifest_url + "\n" + download_error +
            "\n\nCores are loaded as native code, so a tampered download "
            "would run with your account's privileges.\n\nInstall anyway?";
        const SDL_MessageBoxData box{
            SDL_MESSAGEBOX_WARNING, nullptr, "Core installation",
            message.c_str(), SDL_arraysize(buttons), buttons, nullptr};

        int pressed = 0;
        if (SDL_ShowMessageBox(&box, &pressed) != 0 || pressed != 1) {
            return false;
        }
        verified = false;
    }

    emu::frontend::CoreManifest expected;
    if (verified) {
        std::ifstream manifest(manifest_temp);
        expected = emu::frontend::parseCoreManifest(manifest);
    }
    std::error_code ignored;
    fs::remove(manifest_temp, ignored);

    if (verified && expected.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Core installation",
            ("manifest.txt was downloaded but contains no usable entries.\n\n" +
             manifest_url +
             "\n\nEach line must be a 64-character SHA-256 followed by a bare "
             "filename, for example:\n\n    <sha256>  swanstation_libretro.dll")
                .c_str(),
            nullptr);
        return false;
    }

    for (const std::string& filename : files) {
        const auto entry = expected.find(filename);
        if (verified && entry == expected.end()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Core installation",
                ("manifest.txt does not list " + filename +
                 "\n\nThe manifest was read, but has no entry for this file. "
                 "Check that the filenames in it match exactly and carry no "
                 "leading '*'.").c_str(),
                nullptr);
            return false;
        }

        const std::string url = base_url + "/" + filename;
        const fs::path destination = core_directory / filename;
        const fs::path temporary = destination.string() + ".download";
        std::error_code cleanup_error;
        if (!downloadTo(url, temporary, download_error)) {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, "Core installation",
                ("Could not download:\n" + url + "\n" + download_error)
                    .c_str(),
                nullptr);
            return false;
        }

        if (verified) {
            const std::string actual = emu::frontend::sha256File(temporary);
            if (actual.empty() || actual != entry->second) {
                fs::remove(temporary, cleanup_error);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                    "Core installation",
                    ("Checksum mismatch for " + filename +
                     "\n\nexpected " + entry->second +
                     "\ngot      " + (actual.empty() ? "unreadable" : actual) +
                     "\n\nThe download was rejected and deleted.").c_str(),
                    nullptr);
                return false;
            }
        }

        std::error_code replace_error;
        fs::remove(destination, replace_error);
        replace_error.clear();
        fs::rename(temporary, destination, replace_error);
        if (replace_error) {
            fs::remove(temporary, cleanup_error);
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, "Core installation",
                ("Could not install:\n" + destination.string()).c_str(),
                nullptr);
            return false;
        }
    }

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION, "Core installation",
        ("PS1, PSP, and PS2 cores were installed in:\n" +
         core_directory.string()).c_str(), nullptr);
    return true;
#else
    (void)server_url;
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION, "Core installation",
        "Automatic core installation is currently implemented for Windows.",
        nullptr);
    return false;
#endif
}

enum class SelectedSystem {
    Generic,
    PS1,
    PSP,
    PS2,
    Cancel
};

SelectedSystem selectSystemForGame(const std::string& path) {
    std::string extension = fs::path(path).extension().string();

    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".pbp" || extension == ".prx")
        return SelectedSystem::PSP;
    if (extension == ".cue")
        return SelectedSystem::PS1;
    if (extension == ".bin" || extension == ".m3u" || extension == ".ciso" ||
        extension == ".zso" || extension == ".mdf" || extension == ".nrg" ||
        extension == ".dump" || extension == ".img")
        return SelectedSystem::PS2;
    if (extension != ".iso" && extension != ".cso" &&
        extension != ".chd" && extension != ".elf")
        return SelectedSystem::Generic;

    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "PS1"},
        {0, 1, "PSP"},
        {0, 2, "PS2"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 3, "Cancel"}
    };
    SDL_MessageBoxData dialog{};
    dialog.flags = SDL_MESSAGEBOX_INFORMATION;
    dialog.window = nullptr;
    dialog.title = "Choose console";
    dialog.message = "Which system should open this disc image?";
    dialog.numbuttons = static_cast<int>(sizeof(buttons) / sizeof(buttons[0]));
    dialog.buttons = buttons;

    int selected = 3;
    if (SDL_ShowMessageBox(&dialog, &selected) != 0 || selected == 3)
        return SelectedSystem::Cancel;
    if (selected == 0) return SelectedSystem::PS1;
    if (selected == 1) return SelectedSystem::PSP;
    return SelectedSystem::PS2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string rom_path;
    emu::frontend::Settings settings = emu::frontend::loadSettings();
#ifdef _WIN32
    std::string psp_core = "cores/ppsspp_libretro.dll";
    std::string ps1_core = "cores/swanstation_libretro.dll";
#elif __APPLE__
    std::string psp_core = "cores/ppsspp_libretro.dylib";
    std::string ps1_core = "cores/swanstation_libretro.dylib";
#else
    std::string psp_core = "cores/ppsspp_libretro.so";
    std::string ps1_core = "cores/swanstation_libretro.so";
#endif
#ifdef _WIN32
    std::string ps2_core = "cores/pcsx2_libretro.dll";
#elif __APPLE__
    std::string ps2_core = "cores/pcsx2_libretro.dylib";
#else
    std::string ps2_core = "cores/pcsx2_libretro.so";
#endif
    std::string ps2_game_path;
    std::string ps1_game_path;
    std::string system_directory = "system";
    std::string core_server_url =
        "https://8lqb5wp0ujkilwsgmjumf7yi.a.shipwrights.dev/cores/";
    // Verification is on unless the user explicitly opts out. Cores are
    // loaded as native code, so an unverified install is a deliberate choice.
    bool core_server_verify = true;
    std::string library_server_url;
    fs::path games_folder = "games";
    bool mute = settings.mute;
    bool fullscreen = settings.fullscreen;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--mute") {
            mute = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--ppsspp-core" && i + 1 < argc) {
            psp_core = argv[++i];
        } else if (arg == "--ppsspp-core") {
            std::fprintf(stderr, "--ppsspp-core requires a path to the PPSSPP Libretro core\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps1-core" && i + 1 < argc) {
            ps1_core = argv[++i];
        } else if (arg == "--ps1-core") {
            std::fprintf(stderr, "--ps1-core requires a path to the PS1 Libretro core\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps2-core" && i + 1 < argc) {
            ps2_core = argv[++i];
        } else if (arg == "--ps2-core") {
            std::fprintf(stderr, "--ps2-core requires a path to the LRPS2 Libretro core\n");
            return EXIT_FAILURE;
        } else if (arg == "--core-server" && i + 1 < argc) {
            core_server_url = argv[++i];
        } else if (arg == "--input-display") {
            settings.input_display = true;
        } else if (arg == "--core-server-unverified") {
            core_server_verify = false;
        } else if (arg == "--core-server") {
            std::fprintf(stderr, "--core-server requires an HTTPS base URL\n");
            return EXIT_FAILURE;
        } else if (arg == "--library-server" && i + 1 < argc) {
            library_server_url = argv[++i];
        } else if (arg == "--library-server") {
            std::fprintf(stderr, "--library-server requires an HTTP URL\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps2" && i + 1 < argc) {
            ps2_game_path = argv[++i];
        } else if (arg == "--ps2") {
            std::fprintf(stderr, "--ps2 requires a game file path\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps1" && i + 1 < argc) {
            ps1_game_path = argv[++i];
        } else if (arg == "--ps1") {
            std::fprintf(stderr, "--ps1 requires a game file path\n");
            return EXIT_FAILURE;
        } else if (arg == "--system-dir" && i + 1 < argc) {
            system_directory = argv[++i];
        } else if (arg == "--system-dir") {
            std::fprintf(stderr, "--system-dir requires a path\n");
            return EXIT_FAILURE;
        } else if ((arg == "--games-dir" || arg == "--rom") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--games-dir") games_folder = value;
            else rom_path = value;
        } else if (arg == "--games-dir" || arg == "--rom") {
            std::fprintf(stderr, "option '%s' requires a path\n", arg.c_str());
            return EXIT_FAILURE;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return EXIT_FAILURE;
        } else {
            rom_path = arg;
        }
    }

    if ((!ps2_game_path.empty() || !ps1_game_path.empty()) && !rom_path.empty()) {
        std::fprintf(stderr, "Use an explicit system option or --rom, not both.\n");
        return EXIT_FAILURE;
    }
    if (!ps2_game_path.empty() && !ps1_game_path.empty()) {
        std::fprintf(stderr, "Use either --ps1 PATH or --ps2 PATH, not both.\n");
        return EXIT_FAILURE;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    char* raw_base_path = SDL_GetBasePath();
    const fs::path executable_directory = raw_base_path
        ? fs::path(raw_base_path)
        : fs::current_path();
    if (raw_base_path) SDL_free(raw_base_path);

    const fs::path core_directory = executable_directory / "cores";
    const emu::frontend::CoreManager core_manager(core_directory);
    const auto resolveCore = [&](std::string& configured,
                                 emu::frontend::CoreSystem system) {
        if (fs::path(configured).is_absolute()) return;
        const fs::path beside_executable = executable_directory / configured;
        if (fs::exists(beside_executable)) {
            configured = beside_executable.string();
            return;
        }
        if (const auto discovered = core_manager.find(system)) {
            configured = discovered->path.string();
        }
    };
    resolveCore(ps1_core, emu::frontend::CoreSystem::Ps1);
    resolveCore(psp_core, emu::frontend::CoreSystem::Psp);
    resolveCore(ps2_core, emu::frontend::CoreSystem::Ps2);

    if (rom_path.empty() && ps1_game_path.empty() && ps2_game_path.empty()) {
        rom_path = showGameMenu(
            games_folder, settings, system_directory, core_server_url,
            core_server_verify, library_server_url);
        mute = settings.mute;
        fullscreen = settings.fullscreen;
        if (rom_path.empty()) {
            SDL_Quit();
            return EXIT_SUCCESS;
        }
    }

    emu::frontend::CrashLogger logger("logs/toaster.log");
    const auto logLaunch = [&](const char* system,
                               const std::string& core,
                               const std::string& game) {
        logger.writeGameStart(core, game, system);
        if (verbose) {
            std::fprintf(stderr, "[verbose] system=%s core=%s game=%s\n",
                         system, core.c_str(), game.c_str());
        }
    };

    if (!ps1_game_path.empty()) {
        logLaunch("PS1", ps1_core, ps1_game_path);
        const int result = emu::ps1::runPs1Game(
            ps1_core, ps1_game_path, system_directory, fullscreen, mute,
            settings);
        SDL_Quit();
        return result;
    }

    if (!ps2_game_path.empty()) {
        logLaunch("PS2", ps2_core, ps2_game_path);
        const int result = emu::ps2::runPs2Game(
            ps2_core, ps2_game_path, system_directory, fullscreen, mute,
            settings);
        SDL_Quit();
        return result;
    }

    const SelectedSystem selected_system = selectSystemForGame(rom_path);
    if (selected_system == SelectedSystem::Cancel) {
        SDL_Quit();
        return EXIT_SUCCESS;
    }
    if (selected_system == SelectedSystem::PS2) {
        logLaunch("PS2", ps2_core, rom_path);
        const int result = emu::ps2::runPs2Game(
            ps2_core, rom_path, system_directory, fullscreen, mute, settings);
        SDL_Quit();
        return result;
    }
    if (selected_system == SelectedSystem::PS1) {
        logLaunch("PS1", ps1_core, rom_path);
        const int result = emu::ps1::runPs1Game(
            ps1_core, rom_path, system_directory, fullscreen, mute, settings);
        SDL_Quit();
        return result;
    }
    if (selected_system == SelectedSystem::PSP) {
        logLaunch("PSP", psp_core, rom_path);
        const int result = emu::psp::runPspGame(
            psp_core, rom_path, system_directory, fullscreen, mute, settings);
        SDL_Quit();
        return result;
    }

    return runGenericGame(rom_path, mute, fullscreen, settings);
}
