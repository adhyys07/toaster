#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <cmath>
#include <SDL_ttf.h>
#include "core/registry.h"
#include "psp_system.h"
#include "gl_fsr_upscaler.h"
#include <array>
#include "settings.h"
#include "ps2_system.h"

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
        "  --ps2 PATH          launch a PS2 image directly\n"
        "  --ps2-core PATH     path to the LRPS2 Libretro core\n"
        "  --system-dir PATH   system assets/BIOS root (default: system)\n"
        "  --fullscreen        start in fullscreen mode\n"
        "  --mute              mute audio\n"
        "  --help\n"
        "\n"
        "Without a ROM argument, open the game library. O opens a game file.\n"
        "F11 toggles fullscreen; Esc quits during gameplay.\n",
        exe);
}

namespace fs = std::filesystem;

struct GameEntry
{
    fs::path path;
    std::string title;
    std::string developer = "Unknown";
    std::string year = "Unknown";
    std::string genre = "Unknown";
    std::string description = "No description provided.";
    bool favorite = false;
};

std::string gamePathKey(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal().string();
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

bool saveStateToSlot(const emu::System& system,
                     const std::string& rom_path,
                     int slot) {
    std::vector<uint8_t> state;
    if (!system.saveState(state)) return false;
    fs::path path = fs::path(rom_path);
    path.replace_extension(
        ".slot" + std::to_string(slot) + ".state");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    file.write(reinterpret_cast<const char*>(state.data()),
               static_cast<std::streamsize>(state.size()));
    return file.good();
}

bool loadStateFromSlot(emu::System& system,
                       const std::string& rom_path,
                       int slot) {
    fs::path path = fs::path(rom_path);
    path.replace_extension(
        ".slot" + std::to_string(slot) + ".state");
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> state(
        std::istreambuf_iterator<char>(file), {});
    return system.loadState(state);
}

GameEntry readGameEntry(const fs::path& romPath) {
    GameEntry game;
    game.path = romPath;
    game.title = romPath.stem().string();

    std::ifstream metadata(romPath.string() + ".meta");
    std::string line;

    while (std::getline(metadata, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "title") game.title = value;
        else if (key == "developer") game.developer = value;
        else if (key == "year") game.year = value;
        else if (key == "genre") game.genre = value;
        else if (key == "description") game.description = value;
    }

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
            extension == ".iso" || extension == ".cso" || extension == ".pbp" ||
            extension == ".chd" || extension == ".elf" || extension == ".prx" ||
            extension == ".bin" || extension == ".m3u" || extension == ".ciso" ||
            extension == ".zso" || extension == ".mdf" || extension == ".nrg" ||
            extension == ".dump" || extension == ".img") {
            GameEntry game = readGameEntry(item.path());
            game.favorite =
                favorites.count(gamePathKey(game.path)) != 0;
            games.push_back(std::move(game));
        }
    }

    std::sort(games.begin(), games.end(),
        [](const GameEntry& a, const GameEntry& b) {
            return a.title < b.title;
        });

    return games;
}

std::vector<GameEntry> filterGames(const std::vector<GameEntry>& games,
                                   const std::string& query) {
    if (query.empty()) return games;

    std::string wanted = query;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    std::vector<GameEntry> filtered;
    for (const GameEntry& game : games) {
        std::string title = game.title;
        std::transform(title.begin(), title.end(), title.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (title.find(wanted) != std::string::npos) {
            filtered.push_back(game);
        }
    }
    return filtered;
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

SDL_Rect calculateViewport(int window_width, int window_height,
                           int video_width, int video_height,
                           double aspect_ratio,
                           emu::frontend::ScalingMode mode) {
    if (mode == emu::frontend::ScalingMode::Stretch) {
        return {0, 0, window_width, window_height};
    }

    double scale = std::min(
        static_cast<double>(window_width) / video_width,
        static_cast<double>(window_height) / video_height);
    if (mode == emu::frontend::ScalingMode::Integer) {
        scale = std::max(1.0, std::floor(scale));
    }

    int width = static_cast<int>(video_width * scale);
    int height = static_cast<int>(width / aspect_ratio);
    if (height > window_height) {
        height = window_height;
        width = static_cast<int>(height * aspect_ratio);
    }

    return {(window_width - width) / 2, (window_height - height) / 2,
            width, height};
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
        620, 560, SDL_WINDOW_SHOWN);
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
        760, 600, SDL_WINDOW_SHOWN);
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
    const SDL_Rect save_button{410, 535, 125, 44};
    const SDL_Rect cancel_button{555, 535, 125, 44};
    const SDL_Rect controller_button{60, 535, 250, 44};
    
    

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
                         36, 370, muted);
            drawMenuText(renderer, font, "FSR 1 uses OpenGL EASU + RCAS passes.",
                         36, 397, muted);
            drawMenuText(renderer, font, "NES supports mappers 0, 1, 2, 3, 4, 7; MMC3 IRQ timing is approximate.",
                         36, 435, muted, 680);
            drawMenuText(renderer, font, "PICO-8 still requires a separate compatible core.",
                         36, 463, muted, 680);
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

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return saved;
}
std::string chooseRomFile();
bool importPs2Bios(const std::string& system_directory);

std::string showGameMenu(const fs::path& folder,
                         emu::frontend::Settings& settings,
                         const std::string& system_directory) {
    auto games = scanGames(folder);
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
        return {};
    }

    TTF_Font* font = TTF_OpenFont("C:/Windows/Fonts/segoeui.ttf", 20);
    if (!font) font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 20);
    if (!font) {
        std::fprintf(stderr, "Could not open a Windows font: %s\n",
                     TTF_GetError());
        TTF_Quit();
        return {};
    }

    SDL_Window* menu_window = SDL_CreateWindow(
        "Emulator Game Library", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 650, SDL_WINDOW_SHOWN);
    SDL_Renderer* menu_renderer = menu_window
        ? SDL_CreateRenderer(menu_window, -1, SDL_RENDERER_ACCELERATED)
        : nullptr;
    if (!menu_renderer && menu_window) {
        menu_renderer = SDL_CreateRenderer(menu_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!menu_window || !menu_renderer) {
        std::fprintf(stderr, "Could not create game menu: %s\n", SDL_GetError());
        if (menu_renderer) SDL_DestroyRenderer(menu_renderer);
        if (menu_window) SDL_DestroyWindow(menu_window);
        TTF_CloseFont(font);
        TTF_Quit();
        return {};
    }

    size_t selected = 0;
    size_t first_visible = 0;
    constexpr int row_height = 34;
    constexpr int list_top = 128;
    constexpr size_t visible_rows = 12;
    const SDL_Rect launch_button{510, 548, 130, 44};
    const SDL_Rect open_button{650, 548, 140, 44};
    const SDL_Rect rescan_button{800, 548, 150, 44};
    const SDL_Rect settings_button{830, 24, 130, 40};
    const SDL_Rect bios_button{670, 24, 145, 40};
    bool running = true;
    bool redraw = true;
    std::string result;
    std::string search;
    bool search_active = false;

    while (running) {
        if (redraw) {
            const std::vector<GameEntry> visible_games =
                filterGames(games, search);
            const SDL_Color background{13, 18, 28, 255};
            const SDL_Color panel{22, 30, 44, 255};
            const SDL_Color panel_border{45, 59, 78, 255};
            const SDL_Color accent{83, 190, 216, 255};
            const SDL_Color gold{244, 202, 111, 255};
            const SDL_Color white{235, 241, 249, 255};
            const SDL_Color muted{143, 158, 178, 255};

            SDL_SetRenderDrawColor(menu_renderer,
                                   background.r, background.g,
                                   background.b, background.a);
            SDL_RenderClear(menu_renderer);

            TTF_SetFontStyle(font, TTF_STYLE_BOLD);
            drawMenuText(menu_renderer, font, "TOASTER", 28, 18, accent);
            TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
            drawMenuText(menu_renderer, font, "GAME LIBRARY", 28, 48, muted);

            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            drawMenuButton(menu_renderer, font, settings_button, "Settings",
                           mouse_x, mouse_y);
            drawMenuButton(menu_renderer, font, bios_button, "Install PS2 BIOS",
                           mouse_x, mouse_y);

            const SDL_Rect list_panel{20, 94, 440, 442};
            const SDL_Rect details_panel{474, 94, 506, 442};
            drawMenuPanel(menu_renderer, list_panel,
                          panel, panel_border, accent);
            drawMenuPanel(menu_renderer, details_panel,
                          panel, panel_border, gold);

            TTF_SetFontStyle(font, TTF_STYLE_BOLD);
            drawMenuText(menu_renderer, font, "YOUR GAMES", 36, 102, white);
            drawMenuText(menu_renderer, font, "GAME DETAILS", 496, 102, white);
            TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

            const std::string count = std::to_string(visible_games.size()) +
                (visible_games.size() == 1 ? " game" : " games");
            drawMenuText(menu_renderer, font, count, 340, 103, muted);
            drawMenuText(menu_renderer, font,
                         search_active ? "Search: " + search
                                       : "Press / to search",
                         36, 76, search_active ? gold : muted, 400);

            if (visible_games.empty()) {
                drawMenuText(menu_renderer, font, "No games found yet",
                             42, 151, gold);
                drawMenuText(menu_renderer, font,
                             "Add supported games to this folder:",
                             42, 190, white);
                drawMenuText(menu_renderer, font, folder.string(),
                             42, 226, muted, 390);
                drawMenuText(menu_renderer, font,
                             "Press Rescan after adding games.",
                             42, 278, muted);
                drawMenuText(menu_renderer, font, "Nothing selected",
                             500, 154, muted);
            } else {
                for (size_t row = 0;
                     row < visible_rows &&
                     first_visible + row < visible_games.size(); ++row) {
                    const size_t index = first_visible + row;
                    SDL_Rect item{32, list_top + static_cast<int>(row) * row_height,
                                  416, row_height - 4};
                    if (index == selected) {
                        fillRoundedRect(menu_renderer, item, 7,
                                        SDL_Color{43, 76, 98, 255});
                        SDL_Rect marker{item.x, item.y + 5, 3, item.h - 10};
                        SDL_SetRenderDrawColor(menu_renderer,
                                               accent.r, accent.g,
                                               accent.b, accent.a);
                        SDL_RenderFillRect(menu_renderer, &marker);
                    }

                    const std::string label =
                        (visible_games[index].favorite ? "* " : "  ") +
                        visible_games[index].title;
                    drawMenuText(menu_renderer, font, label,
                                 item.x + 14, item.y + 4,
                                 index == selected ? gold : white, 385);
                }

                const GameEntry& game = visible_games[selected];
                TTF_SetFontStyle(font, TTF_STYLE_BOLD);
                drawMenuText(menu_renderer, font, game.title, 500, 148, gold, 450);
                TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
                drawMenuText(menu_renderer, font,
                             "Developer  " + game.developer,
                             500, 204, white, 450);
                drawMenuText(menu_renderer, font, "Year       " + game.year,
                             500, 240, white);
                drawMenuText(menu_renderer, font, "Genre      " + game.genre,
                             500, 276, white, 450);

                SDL_Rect separator{500, 316, 450, 1};
                SDL_SetRenderDrawColor(menu_renderer,
                                       panel_border.r, panel_border.g,
                                       panel_border.b, panel_border.a);
                SDL_RenderFillRect(menu_renderer, &separator);
                drawMenuText(menu_renderer, font, "DESCRIPTION",
                             500, 336, muted);
                drawMenuText(menu_renderer, font, game.description,
                             500, 370, white, 445);
                drawMenuText(menu_renderer, font,
                             game.favorite ? "Favorite" : "Not a favorite",
                             500, 470, game.favorite ? gold : muted);
            }

            drawMenuButton(menu_renderer, font, launch_button, "Launch",
                           mouse_x, mouse_y);
            drawMenuButton(menu_renderer, font, open_button, "Open Game",
                           mouse_x, mouse_y);
            drawMenuButton(menu_renderer, font, rescan_button, "Rescan",
                           mouse_x, mouse_y);
            drawMenuText(menu_renderer, font,
                         "F toggles favorite. / searches. Double-click or Enter launches.",
                         28, 602, muted, 820);

            SDL_RenderPresent(menu_renderer);
            redraw = false;
        }

        SDL_Event event;
        if (!SDL_WaitEvent(&event)) continue;

        do {
            const std::vector<GameEntry> visible_games =
                filterGames(games, search);
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEMOTION:
                redraw = true;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (search_active) {
                        search.clear();
                        search_active = false;
                        SDL_StopTextInput();
                        selected = 0;
                        first_visible = 0;
                        redraw = true;
                    } else {
                        running = false;
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (search_active && !search.empty()) {
                        search.pop_back();
                        selected = 0;
                        first_visible = 0;
                        redraw = true;
                    }
                    break;
                case SDLK_SLASH:
                    search_active = true;
                    SDL_StartTextInput();
                    redraw = true;
                    break;
                case SDLK_f:
                    if (!visible_games.empty()) {
                        const std::string path =
                            gamePathKey(visible_games[selected].path);
                        for (GameEntry& game : games) {
                            if (gamePathKey(game.path) == path) {
                                game.favorite = !game.favorite;
                                break;
                            }
                        }
                        if (!saveFavorites(games)) {
                            std::fprintf(stderr,
                                         "Could not save favorites\n");
                        }
                        redraw = true;
                    }
                    break;
                case SDLK_UP:
                    if (selected > 0 && !visible_games.empty()) --selected;
                    redraw = true;
                    break;
                case SDLK_DOWN:
                    if (selected + 1 < visible_games.size()) ++selected;
                    redraw = true;
                    break;
                case SDLK_r:
                    games = scanGames(folder);
                    selected = 0;
                    first_visible = 0;
                    redraw = true;
                    break;
                case SDLK_s:
                    showSettingsMenu(settings, font);
                    redraw = true;
                    break;
                case SDLK_b:
                    importPs2Bios(system_directory);
                    redraw = true;
                    break;
                case SDLK_o: {
                    const std::string chosen_rom = chooseRomFile();
                    if (!chosen_rom.empty()) {
                        result = chosen_rom;
                        running = false;
                    }
                    break;
                }
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (!visible_games.empty()) {
                        result = visible_games[selected].path.string();
                        running = false;
                    }
                break;
                default:
                    break;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (event.wheel.y > 0 && selected > 0 &&
                    !visible_games.empty()) --selected;
                if (event.wheel.y < 0 &&
                    selected + 1 < visible_games.size()) ++selected;
                redraw = true;
                break;
            case SDL_TEXTINPUT:
                if (search_active) {
                    search += event.text.text;
                    selected = 0;
                    first_visible = 0;
                    redraw = true;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button != SDL_BUTTON_LEFT) break;

                if (pointInside(settings_button, event.button.x, event.button.y)) {
                    showSettingsMenu(settings, font);
                    redraw = true;
                    break;
                }

                if (pointInside(bios_button, event.button.x, event.button.y)) {
                    importPs2Bios(system_directory);
                    redraw = true;
                    break;
                }

                if (pointInside(launch_button, event.button.x, event.button.y)) {
                    if (!visible_games.empty()) {
                        result = visible_games[selected].path.string();
                        running = false;
                    }
                    break;
                }

                if (pointInside(open_button, event.button.x, event.button.y)) {
                    const std::string chosen_rom = chooseRomFile();
                    if (!chosen_rom.empty()) {
                        result = chosen_rom;
                        running = false;
                    }
                    break;
                }

                if (pointInside(rescan_button, event.button.x, event.button.y)) {
                    games = scanGames(folder);
                    selected = 0;
                    first_visible = 0;
                    redraw = true;
                    break;
                }

                if (event.button.x >= 24 && event.button.x < 444 &&
                    event.button.y >= list_top &&
                    event.button.y < list_top + static_cast<int>(visible_rows) * row_height) {
                    const size_t row = static_cast<size_t>(
                        (event.button.y - list_top) / row_height);
                    const size_t index = first_visible + row;
                    if (index < visible_games.size()) {
                        selected = index;
                        redraw = true;
                        if (event.button.clicks >= 2) {
                            result = visible_games[index].path.string();
                            running = false;
                        }
                    }
                }
                break;
            default:
                break;
            }

            first_visible = selected >= visible_rows
                ? selected - visible_rows + 1
                : 0;
        } while (SDL_PollEvent(&event));
    }

    SDL_DestroyRenderer(menu_renderer);
    SDL_DestroyWindow(menu_window);
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

int runGenericGame(const std::string& rom_path,
                   bool mute,
                   bool start_fullscreen,
                   const emu::frontend::Settings& settings) {
    std::string error;
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

    const bool use_fsr = settings.upscaler == emu::frontend::Upscaler::Fsr1;
    if (use_fsr) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (use_fsr) window_flags |= SDL_WINDOW_OPENGL;
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
    if (use_fsr) {
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
            if (fs::exists(executable_shader_directory / "fsr_easu.frag")) {
                shader_directory = executable_shader_directory;
            }
            SDL_free(base_path);
        }

        if (!fsr.initialize(window, shader_directory.string(), error)) {
            std::fprintf(stderr, "FSR initialization failed: %s\n", error.c_str());
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
            SDL_RenderSetLogicalSize(renderer, texture_width, texture_height);
        }

        return SDL_UpdateTexture(texture, nullptr, frame.pixels,
                                 texture_width * static_cast<int>(sizeof(uint32_t))) == 0;
    };

    if (!use_fsr && !updateTexture()) {
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
        wanted.samples = 1024;
        audio = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);
        else std::fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
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
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       event.key.keysym.scancode == SDL_SCANCODE_F11) {
                const bool is_fullscreen =
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                if (SDL_SetWindowFullscreen(
                        window,
                        is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
                    std::fprintf(stderr, "Could not toggle fullscreen: %s\n", SDL_GetError());
                }
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_p) {
                    paused = !paused;
                } else if (key == SDLK_n && paused) {
                    frame_advance = true;
                } else if (key == SDLK_TAB) {
                    fast_forward = !fast_forward;
                } else if (key >= SDLK_F1 && key <= SDLK_F9) {
                    const int slot = static_cast<int>(key - SDLK_F1) + 1;
                    if (!saveStateToSlot(*system, rom_path, slot)) {
                        std::fprintf(stderr,
                                     "Save state slot %d is unsupported or failed\n",
                                     slot);
                    }
                } else if (key >= SDLK_1 && key <= SDLK_9 &&
                           (event.key.keysym.mod & KMOD_CTRL)) {
                    const int slot = static_cast<int>(key - SDLK_1) + 1;
                    if (!loadStateFromSlot(*system, rom_path, slot)) {
                        std::fprintf(stderr,
                                     "Load state slot %d is unsupported or failed\n",
                                     slot);
                    }
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
        const std::string system_name = normalizedButtonName(system->name());
        const bool is_pico8 = system_name == "PICO-8" || system_name == "PICO8";

        for (int port = 0; port < system->portCount(); ++port) {
            uint32_t button_state = 0;

            int button_index = 0;
            for (const emu::ButtonInfo& button : system->buttons()) {
                bool down = false;
                const std::string button_name = normalizedButtonName(button.name);

                // Player 1 uses the keyboard bindings declared by the core.
                if (port == 0 && button_index < emu::frontend::kPadActionCount) {
                    const SDL_Scancode key = static_cast<SDL_Scancode>(
                        settings.keyboard_keys[button_index]);
                    if (key != SDL_SCANCODE_UNKNOWN && keyboard[key]) { down = true };
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
                if (!paused || frame_advance){
                    system->runFrame();
                    frame_advance = false;
                }
                if (down) button_state |= button.mask;
                ++button_index;
            }

            system->setInput(port, button_state);
        }

        if (!paused || frame_advance) {
            system->runFrame();
            frame_advance = false;
        }
        if (use_fsr) {
            if (!fsr.render(system->video(), system->aspectRatio(),
                            settings.sharpness, error)) {
                std::fprintf(stderr, "FSR render failed: %s\n", error.c_str());
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
            const SDL_Rect viewport = calculateViewport(
                window_width, window_height, frame.width, frame.height,
                system->aspectRatio(), settings.scaling);
            SDL_RenderCopy(renderer, texture, nullptr, &viewport);
            SDL_RenderPresent(renderer);
        }

        if (settings.show_fps) {
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
                if (SDL_GetQueuedAudioSize(audio) >
                    static_cast<Uint32>(system->sampleRate() * sizeof(float) / 4)) {
                    SDL_ClearQueuedAudio(audio);
                }
                SDL_QueueAudio(audio, audio_buffer.data(),
                               static_cast<Uint32>(count * sizeof(float)));
            }
        }

        const double speed_multiplier = fast_forward ? 4.0 : 1.0;
        next_frame += target_frame_ticks / speed_multiplier;

        next_frame += target_frame_ticks / (fast_forward ? 4.0 : 1.0);
        const double now = static_cast<double>(SDL_GetPerformanceCounter());
        if (next_frame > now) {
            const Uint32 delay_ms = static_cast<Uint32>(
                (next_frame - now) * 1000.0 / performance_frequency);
            if (delay_ms > 0) SDL_Delay(delay_ms);
        } else {
            next_frame = now;
        }
    }

    for (SDL_GameController* gamepad : gamepads) {
        if (gamepad) SDL_GameControllerClose(gamepad);
    }
    if (audio) SDL_CloseAudioDevice(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (gl_context) {
        fsr.shutdown();
        SDL_GL_DeleteContext(gl_context);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

std::string chooseRomFile() {
#ifdef _WIN32
    char filename[32768] = {};

    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter =
        "Supported games\0*.nes;*.ch8;*.c8;*.iso;*.cso;*.pbp;*.chd;*.elf;*.prx;*.bin;*.m3u;*.ciso;*.zso;*.mdf;*.nrg;*.dump;*.img\0"
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

bool importPs2Bios(const std::string& system_directory) {
#ifdef _WIN32
    char filename[32768] = {};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter =
        "PS2 BIOS images (*.bin;*.rom;*.bios)\0*.bin;*.rom;*.bios\0"
        "All files\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = "Select a PS2 BIOS dump you own";

    if (!GetOpenFileNameA(&dialog)) return false;

    std::error_code error;
    const fs::path source = fs::absolute(fs::path(filename), error);
    if (error || !fs::is_regular_file(source, error) || error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PS2 BIOS",
                                 "The selected file could not be read.", nullptr);
        return false;
    }

    fs::path base_path;
    if (char* raw_base = SDL_GetBasePath()) {
        base_path = raw_base;
        SDL_free(raw_base);
    }

    const fs::path requested_system(system_directory);
    std::vector<fs::path> candidates{requested_system};
    if (!requested_system.is_absolute() && !base_path.empty()) {
        candidates.push_back(base_path / requested_system);
        candidates.push_back(base_path.parent_path() / requested_system);
    }

    fs::path system_root;
    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::is_directory(candidate, error) && !error) {
            system_root = candidate;
            break;
        }
    }
    if (system_root.empty()) {
        system_root = requested_system.is_absolute()
            ? requested_system
            : (base_path.empty() ? fs::current_path() : base_path) / requested_system;
    }

    const fs::path bios_directory = system_root / "pcsx2" / "bios";
    error.clear();
    fs::create_directories(bios_directory, error);
    if (error) {
        const std::string message = "Could not create the BIOS folder:\n" +
                                    bios_directory.string() + "\n\n" + error.message();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PS2 BIOS",
                                 message.c_str(), nullptr);
        return false;
    }

    const fs::path destination = bios_directory / source.filename();
    error.clear();
    if (fs::equivalent(source, destination, error) && !error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "PS2 BIOS",
                                 "That BIOS file is already in the emulator's BIOS folder.",
                                 nullptr);
        return true;
    }

    error.clear();
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    if (error) {
        const std::string message = "Could not copy the BIOS file:\n" + error.message();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PS2 BIOS",
                                 message.c_str(), nullptr);
        return false;
    }

    const std::string message = "BIOS imported to:\n" + destination.string() +
        "\n\nRestart the game if it is already running.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "PS2 BIOS",
                             message.c_str(), nullptr);
    return true;
#else
    (void)system_directory;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "PS2 BIOS",
        "Use the system file picker to copy your legally dumped BIOS into "
        "system/pcsx2/bios.", nullptr);
    return false;
#endif
}

enum class SelectedSystem {
    Generic,
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
    if (extension == ".bin" || extension == ".m3u" || extension == ".ciso" ||
        extension == ".zso" || extension == ".mdf" || extension == ".nrg" ||
        extension == ".dump" || extension == ".img")
        return SelectedSystem::PS2;
    if (extension != ".iso" && extension != ".cso" &&
        extension != ".chd" && extension != ".elf")
        return SelectedSystem::Generic;

    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "PSP"},
        {0, 1, "PS2"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel"}
    };
    SDL_MessageBoxData dialog{};
    dialog.flags = SDL_MESSAGEBOX_INFORMATION;
    dialog.window = nullptr;
    dialog.title = "Choose console";
    dialog.message = "This file type can be used by PSP or PS2. Which system should open it?";
    dialog.numbuttons = static_cast<int>(sizeof(buttons) / sizeof(buttons[0]));
    dialog.buttons = buttons;

    int selected = 2;
    if (SDL_ShowMessageBox(&dialog, &selected) != 0 || selected == 2)
        return SelectedSystem::Cancel;
    return selected == 1 ? SelectedSystem::PS2 : SelectedSystem::PSP;
}

}  // namespace

int main(int argc, char** argv) {
    std::string rom_path;
    emu::frontend::Settings settings = emu::frontend::loadSettings();
#ifdef _WIN32
    std::string psp_core = "cores/ppsspp_libretro.dll";
#elif __APPLE__
    std::string psp_core = "cores/ppsspp_libretro.dylib";
#else
    std::string psp_core = "cores/ppsspp_libretro.so";
#endif
#ifdef _WIN32
    std::string ps2_core = "cores/pcsx2_libretro.dll";
#elif __APPLE__
    std::string ps2_core = "cores/pcsx2_libretro.dylib";
#else
    std::string ps2_core = "cores/pcsx2_libretro.so";
#endif
    std::string ps2_game_path;
    std::string system_directory = "system";
    fs::path games_folder = "games";
    bool mute = settings.mute;
    bool fullscreen = settings.fullscreen;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--mute") {
            mute = true;
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--ppsspp-core" && i + 1 < argc) {
            psp_core = argv[++i];
        } else if (arg == "--ppsspp-core") {
            std::fprintf(stderr, "--ppsspp-core requires a path to the PPSSPP Libretro core\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps2-core" && i + 1 < argc) {
            ps2_core = argv[++i];
        } else if (arg == "--ps2-core") {
            std::fprintf(stderr, "--ps2-core requires a path to the LRPS2 Libretro core\n");
            return EXIT_FAILURE;
        } else if (arg == "--ps2" && i + 1 < argc) {
            ps2_game_path = argv[++i];
        } else if (arg == "--ps2") {
            std::fprintf(stderr, "--ps2 requires a game file path\n");
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

    if (!ps2_game_path.empty() && !rom_path.empty()) {
        std::fprintf(stderr, "Use either --ps2 PATH or --rom PATH, not both.\n");
        return EXIT_FAILURE;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (rom_path.empty() && ps2_game_path.empty()) {
        rom_path = showGameMenu(games_folder, settings, system_directory);
        mute = settings.mute;
        fullscreen = settings.fullscreen;
        if (rom_path.empty()) {
            SDL_Quit();
            return EXIT_SUCCESS;
        }
    }

    if (!ps2_game_path.empty()) {
        const int result = emu::ps2::runPs2Game(
            ps2_core, ps2_game_path, system_directory, fullscreen, mute);
        SDL_Quit();
        return result;
    }

    const SelectedSystem selected_system = selectSystemForGame(rom_path);
    if (selected_system == SelectedSystem::Cancel) {
        SDL_Quit();
        return EXIT_SUCCESS;
    }
    if (selected_system == SelectedSystem::PS2) {
        const int result = emu::ps2::runPs2Game(
            ps2_core, rom_path, system_directory, fullscreen, mute);
        SDL_Quit();
        return result;
    }
    if (selected_system == SelectedSystem::PSP) {
        const int result = emu::psp::runPspGame(
            psp_core, rom_path, system_directory, fullscreen, mute);
        SDL_Quit();
        return result;
    }

    return runGenericGame(rom_path, mute, fullscreen, settings);
}
