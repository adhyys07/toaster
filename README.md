# Toaster

A retro-game emulator that runs five consoles in a single window.

Most emulators handle a single system, so playing across a collection means
installing, configuring and learning several separate programs. Toaster compiles
NES, Game Boy, CHIP-8, PlayStation 1, PSP and PlayStation 2 on one window, one settings screen and one set of controls.

## What it does

- **One library for everything.** Scans a folder and launches any supported
  game, with cover art, search, favourites, recently played and per-system
  filters.
- **Emulation written from scratch** for NES, Game Boy and CHIP-8 — CPU, video
  and audio are all in this repository. PlayStation and PSP run through
  external plug-in cores, which Toaster can download and verify for you.
- **Save states and rewind.** A visual slot grid shows a thumbnail and a
  timestamp for every save, and holding one key rewinds the game.
- **Everything stays in the window.** Settings, controller binding, cheats,
  save states and themes are overlays drawn over the game, not extra windows.
- **Six themes and an editor** that checks colour contrast as you edit, so a
  custom theme cannot come out unreadable.
- **A live controller diagram** showing exactly what the pad is doing. It
  drives button binding, and can stay on screen while you play.

## Try it

You need CMake, Ninja, a C++17 compiler, SDL2, SDL2_ttf, SDL2_image and
libcurl.

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
build/emu.exe
```

That opens the library, which reads the `games/` folder. Drop a `.nes`, `.gb`
or `.ch8` file in there and press Enter — those three systems are built in and
need nothing else. PlayStation and PSP also need their own BIOS and plug-in
cores, neither of which Toaster ships.

Press **F1** during a game for the pause menu: saves, settings, controls,
cheats and themes all live there.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Seven suites covering the CPU cores, save-state round-trips, cheat decoding,
settings files and the interface — including a contrast check on every theme
and geometry checks that catch overlapping on-screen elements before they can
reach a display.

## Why I built it

I got tired of installing a separate emulator for every console just to play
one more favourite game. Toaster puts five of them in one place.

## More

[docs/manual.md](docs/manual.md) is the full reference: command-line flags,
every hotkey, BIOS layout, the cheat format, per-game settings, the core
installer and the optional library service.

## Legal notice

Toaster includes no games, BIOS files, firmware or third-party emulator cores.
Use only backups and software you are legally permitted to use.
