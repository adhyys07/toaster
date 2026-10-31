#include <cctype>
#include <filesystem>
#include <fstream>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <SDL_ttf.h>
#include "core/registry.h"
#include "psp_system.h"
#include <array>

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
        "  --system-dir PATH   PPSSPP system assets folder (default: system)\n"
        "  --fullscreen        start in fullscreen mode\n"
        "  --mute              mute audio\n"
        "  --help\n"
        "\n"
        "Without a ROM argument, open the game library. O opens a ROM file.\n"
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
};

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

    if (!fs::exists(folder, error) || error) return games;

    for (const auto& item : fs::directory_iterator(folder, error)) {
        if (error) break;
        if (!item.is_regular_file()) continue;

        std::string extension = item.path().extension().string();
        for (char& ch : extension) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }

        if (extension == ".ch8" || extension == ".c8" || extension == ".nes" ||
            extension == ".iso" || extension == ".cso" || extension == ".pbp" ||
            extension == ".chd" || extension == ".elf" || extension == ".prx") {
            games.push_back(readGameEntry(item.path()));
        }
    }

    std::sort(games.begin(), games.end(),
        [](const GameEntry& a, const GameEntry& b) {
            return a.title < b.title;
        });

    return games;
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

bool pointInside(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

void drawMenuButton(SDL_Renderer* renderer, TTF_Font* font,
                    const SDL_Rect& rect, const std::string& label,
                    int mouse_x, int mouse_y) {
    const bool hover = pointInside(rect, mouse_x, mouse_y);

    if (hover) {
        SDL_SetRenderDrawColor(renderer, 100, 122, 140, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 55, 68, 80, 255);
    }
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, 150, 165, 178, 255);
    SDL_RenderDrawRect(renderer, &rect);

    int text_width = 0;
    int text_height = 0;
    TTF_SizeUTF8(font, label.c_str(), &text_width, &text_height);

    const SDL_Color text_color{245, 247, 249, 255};
    drawMenuText(renderer, font, label,
                 rect.x + (rect.w - text_width) / 2,
                 rect.y + (rect.h - text_height) / 2,
                 text_color);
}

std::string chooseRomFile();

std::string showGameMenu(const fs::path& folder) {
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
    constexpr int list_top = 112;
    constexpr size_t visible_rows = 14;
    const SDL_Rect launch_button{510, 548, 130, 44};
    const SDL_Rect open_button{650, 548, 140, 44};
    const SDL_Rect rescan_button{800, 548, 150, 44};
    bool running = true;
    bool redraw = true;
    std::string result;

    while (running) {
        if (redraw) {
            SDL_SetRenderDrawColor(menu_renderer, 17, 22, 28, 255);
            SDL_RenderClear(menu_renderer);

            const SDL_Color gold{233, 216, 166, 255};
            const SDL_Color white{235, 238, 240, 255};
            const SDL_Color muted{160, 170, 180, 255};

            drawMenuText(menu_renderer, font, "GAME LIBRARY",
                         32, 24, gold);
            drawMenuText(menu_renderer, font,
             "Click a game, then Launch. O opens a ROM. Esc quits.",
             32, 65, muted);

            SDL_Rect divider{470, 105, 2, 500};
            SDL_SetRenderDrawColor(menu_renderer, 70, 80, 90, 255);
            SDL_RenderFillRect(menu_renderer, &divider);

            if (games.empty()) {
                drawMenuText(menu_renderer, font, "No games found",
                             36, list_top + 10, gold);
                drawMenuText(menu_renderer, font,
                             "Add NES, CHIP-8, or PSP games to:",
                             36, list_top + 50, white);
                drawMenuText(menu_renderer, font, folder.string(),
                             36, list_top + 80, muted);
                drawMenuText(menu_renderer, font, "Press R to scan again.",
                             36, list_top + 120, muted);
                drawMenuText(menu_renderer, font, "No game selected",
                             510, 120, muted);
                drawMenuText(menu_renderer, font,
                             "Add a supported ROM to the games folder, then press R.",
                             510, 170, white, 440);
            } else {
                for (size_t row = 0;
                     row < visible_rows && first_visible + row < games.size(); ++row) {
                    const size_t index = first_visible + row;
                    SDL_Rect item{24, list_top + static_cast<int>(row) * row_height,
                                  420, row_height - 2};
                    if (index == selected) {
                        SDL_SetRenderDrawColor(menu_renderer, 75, 91, 105, 255);
                        SDL_RenderFillRect(menu_renderer, &item);
                    }

                    drawMenuText(menu_renderer, font, games[index].title,
                                 36, item.y + 5,
                                 index == selected ? gold : white);
                }

                const GameEntry& game = games[selected];
                drawMenuText(menu_renderer, font, game.title, 510, 120, gold);
                drawMenuText(menu_renderer, font, "Developer: " + game.developer,
                             510, 170, white);
                drawMenuText(menu_renderer, font, "Year: " + game.year,
                             510, 210, white);
                drawMenuText(menu_renderer, font, "Genre: " + game.genre,
                             510, 250, white);
                drawMenuText(menu_renderer, font, "Description:", 510, 305, muted);
                drawMenuText(menu_renderer, font, game.description,
                             510, 340, white, 440);
            }

            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            drawMenuButton(menu_renderer, font, launch_button, "Launch",
                           mouse_x, mouse_y);
            drawMenuButton(menu_renderer, font, open_button, "Open ROM",
                           mouse_x, mouse_y);
            drawMenuButton(menu_renderer, font, rescan_button, "Rescan",
                           mouse_x, mouse_y);

            SDL_RenderPresent(menu_renderer);
            redraw = false;
        }

        SDL_Event event;
        if (!SDL_WaitEvent(&event)) continue;

        do {
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
                    running = false;
                    break;
                case SDLK_UP:
                    if (selected > 0) --selected;
                    redraw = true;
                    break;
                case SDLK_DOWN:
                    if (selected + 1 < games.size()) ++selected;
                    redraw = true;
                    break;
                case SDLK_r:
                    games = scanGames(folder);
                    selected = 0;
                    first_visible = 0;
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
                    if (!games.empty()) {
                        result = games[selected].path.string();
                        running = false;
                    }
                break;
                default:
                    break;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (event.wheel.y > 0 && selected > 0) --selected;
                if (event.wheel.y < 0 && selected + 1 < games.size()) ++selected;
                redraw = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button != SDL_BUTTON_LEFT) break;

                if (pointInside(launch_button, event.button.x, event.button.y)) {
                    if (!games.empty()) {
                        result = games[selected].path.string();
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
                    if (index < games.size()) {
                        selected = index;
                        redraw = true;
                        if (event.button.clicks >= 2) {
                            result = games[index].path.string();
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
                        bool is_pico8){
    if (!controller) return false;

    const std::string name = normalizedButtonName(button_name);

    const auto pressed = [controller](SDL_GameControllerButton button){
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
    
    SDL_Scancode nesPlayerTwoKey(const char* button_name){
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

int runGenericGame(const std::string& rom_path, bool mute, bool start_fullscreen) {
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

    std::string title = std::string(system->name()) + " - " + rom_path;

    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
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

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
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

    if (!updateTexture()) {
        std::fprintf(stderr, "Could not create the game's video texture: %s\n", SDL_GetError());
        if (texture) SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
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
            } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                // SDL provides a device index for controller-added events.
                openGamepad(event.cdevice.which);
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                // Removal events provide the joystick instance ID.
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

            for (const emu::ButtonInfo& button : system->buttons()) {
                bool down = false;
                const std::string button_name =
                    normalizedButtonName(button.name);

                // Player 1 uses the core's declared keyboard bindings.
                if (port == 0 && button.default_key) {
                    const SDL_Scancode key =
                        SDL_GetScancodeFromName(button.default_key);
                    if (key != SDL_SCANCODE_UNKNOWN && keyboard[key]) {
                        down = true;
                    }
                }

                // PICO-8's two action buttons have common keyboard aliases.
                if (port == 0 && is_pico8) {
                    if (button_name == "O") {
                        down = down || keyboard[SDL_SCANCODE_Z] ||
                               keyboard[SDL_SCANCODE_C];
                    } else if (button_name == "X") {
                        down = down || keyboard[SDL_SCANCODE_X] ||
                               keyboard[SDL_SCANCODE_V];
                    }
                }

                // Optional second-player keyboard layout for the NES.
                if (port == 1 && system_name == "NES") {
                    const SDL_Scancode key = nesPlayerTwoKey(button.name);
                    if (key != SDL_SCANCODE_UNKNOWN && keyboard[key]) {
                        down = true;
                    }
                }

                // Controller slot 0 drives port 0; slot 1 drives port 1.
                if (port < static_cast<int>(gamepads.size()) && gamepads[port] &&
                    gamepadButtonDown(gamepads[port], button.name, is_pico8)) {
                    down = true;
                }

                if (down) button_state |= button.mask;
            }

            system->setInput(port, button_state);
        }

        system->runFrame();
        if (!updateTexture()) {
            std::fprintf(stderr, "Could not update the game image: %s\n", SDL_GetError());
            break;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

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

        next_frame += target_frame_ticks;
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
    SDL_DestroyRenderer(renderer);
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
        "Supported ROMs\0*.nes;*.ch8;*.c8;*.iso;*.cso;*.pbp;*.chd;*.elf;*.prx\0"
        "PSP games\0*.iso;*.cso;*.pbp;*.chd;*.elf;*.prx\0"
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

bool isPspRom(const std::string& path) {
    std::string extension = fs::path(path).extension().string();

    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension == ".iso" || extension == ".cso" ||
           extension == ".pbp" || extension == ".chd" ||
           extension == ".elf" || extension == ".prx";
}

}  // namespace

int main(int argc, char** argv) {
    std::string rom_path;
#ifdef _WIN32
    std::string psp_core = "cores/ppsspp_libretro.dll";
#elif __APPLE__
    std::string psp_core = "cores/ppsspp_libretro.dylib";
#else
    std::string psp_core = "cores/ppsspp_libretro.so";
#endif
    std::string system_directory = "system";
    fs::path games_folder = "games";
    bool mute = false;
    bool fullscreen = false;

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

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (rom_path.empty()) {
        rom_path = showGameMenu(games_folder);
        if (rom_path.empty()) {
            SDL_Quit();
            return EXIT_SUCCESS;
        }
    }

    if (isPspRom(rom_path)) {
        const int result = emu::psp::runPspGame(
            psp_core, rom_path, system_directory, fullscreen, mute);
        SDL_Quit();
        return result;
    }

    return runGenericGame(rom_path, mute, fullscreen);
}
