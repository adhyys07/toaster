# Toaster

A retro-game emulator that runs six consoles in a single window.

Most emulators handle a single system, so playing across a collection means
installing, configuring and learning several separate programs. Toaster compiles
NES, Game Boy (along with color), CHIP-8, PlayStation 1, WASM4, PSP and PlayStation 2 on one window, one settings screen and one set of controls.

## In-Action
![Alt text describing the image](https://cdn.hackclub.com/01a1bcb6-50fb-7e85-bd23-42e3a5dd33a4/Screenshot%202026-11-09%20012005.jpg)

## What it does

- **One library for everything.** Scans a folder and launches any supported
  game, with cover art, search, favourites, recently played and per-system
  filters.
- **Core emulation** for NES, Game Boy, WASM4 and CHIP-8: CPU, video
  and audio are all in this repository. PlayStation and PSP run through
  external libraries, which Toaster can download from its dedicated server.
- **Save states and rewind.** A visual slot grid shows a thumbnail and a
  timestamp for every save, and holding one key rewinds the game.
- **Everything stays in the window.** Settings, controller binding, cheats,
  save states and themes are overlays drawn over the game, not extra windows.
- **Themes and an editor** The user can choose the theme for his emulator whatever way he likes, apart from this there is a built in editor in which he can edit the predefined themes and adjust accordingly
- **Controller Checker** A controller GUI which can help you map the right keys to it and makes easier by showing each moment on the GUI screen 

## Try it

You need CMake, Ninja, a C++23 compiler, SDL2, SDL2_ttf, SDL2_image and
libcurl.

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
build/emu.exe
```

That opens the library, which reads the `games/` folder. Drop a `.nes`, `.gb`
or `.ch8` file in there and press Enter, those three systems are built in and
need nothing else. PlayStation and PSP also need their own BIOS and plug-in
cores, neither of which Toaster ships.

Press **F1** during a game for the pause menu: saves, settings, controls,
cheats and themes all live there.


## Why I built it

I got tired of installing a separate emulator for every console just to play
one more favourite game. Toaster puts six of them in one place so that I can enjoy most of my games in a single window.

## More

[docs/manual.md](docs/manual.md) is the full reference: command-line flags,
every hotkey, BIOS layout, the cheat format, per-game settings, the core
installer and the optional library service.

## Legal notice

Toaster includes no games, BIOS files, firmware or third-party emulator cores.
Use only backups and software you are legally permitted to use.
