# Toaster manual

The full reference. [The README](../README.md) is the short version.

Toaster runs NES, Game Boy and CHIP-8 on built-in cores, and PlayStation 1,
PSP and PlayStation 2 through external Libretro cores.

## Build

Needs CMake, Ninja, a C++17 compiler, Python 3, SDL2, SDL2_ttf, SDL2_image and
libcurl.

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Ubuntu/Debian the dependencies are:

```bash
sudo apt install build-essential cmake ninja-build python3 \
  libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
  libcurl4-openssl-dev libgl-dev
```

Linux Libretro cores use `.so` rather than `.dll`.

## Run

Start with no arguments to open the game library, which scans `games/`:

```bash
build/emu.exe
build/emu.exe --games-dir "/path/to/games"
```

Or launch straight into a game. PS1 and PS2 take their own flag so disc images
are not mistaken for PSP ISOs:

```bash
build/emu.exe "/path/to/game.nes"
build/emu.exe --ps1 "/path/to/game.cue" --system-dir "/path/to/system"
build/emu.exe --ps2 "/path/to/game.iso" --system-dir "/path/to/system"
build/emu.exe "/path/to/game.iso" --system-dir "/path/to/system"   # PSP
```

Install your own legally dumped BIOS and assets under the system directory:

| System | Path |
|---|---|
| PS1 | `system/swanstation/bios/` |
| PS2 | `system/pcsx2/bios/` |
| PSP | `system/PPSSPP/` |

BIOS files are private firmware. Toaster neither distributes nor uploads them.

Other flags: `--fullscreen`, `--mute`, `--verbose`, `--resume` (continue from
the auto-save left by the last clean exit) and `--input-display` (a live
controller read-out over the game).

## Controls

F1 opens the pause menu, which also holds the video, audio, controller,
keyboard, cheat and per-game settings. While it is open it takes the keyboard
and the controller, so nothing reaches the game.

| Action | Key |
|---|---|
| Pause menu | F1 |
| Save-state grid | F3 |
| Save / load state, slots 1-9 | Shift + F1-F9 / Ctrl + 1-9 |
| Quick save / load | F5 / F8 |
| Rewind | Hold Backspace |
| Pause / frame advance | P / N |
| Fast forward | Tab |
| Volume down / up / mute | - / = / M |
| Screenshot / gallery | F10 / Shift + F10 |
| Toggle cheats | F9 |
| Input display | F2 |
| Fullscreen | F11 |
| Reset | Ctrl + R |
| Exit | Escape |

Every single-key action above is rebindable on the pause menu's Keyboard page
and is stored in `hotkeys.ini` beside the executable; the numbered slot ranges
are fixed. Game buttons are bound on the Controller page, which draws a live
diagram of the connected pad.

With a Libretro core the keyboard defaults are arrow keys or numpad 2/4/6/8
for the D-pad, W/A/S/D and I/J/K/L for the sticks, Z/X/C/V for
Cross/Circle/Square/Triangle, Q/E for L1/R1, 1/3 for L2/R2, and
Enter/Backspace for Start/Select.

SDL-compatible game controllers work out of the box.

## Files beside a game

| File | Holds |
|---|---|
| `<game>.ini` | Per-game setting overrides |
| `<game>.cht` | Cheats |
| `<game>.srm`, `<game>.sav` | Game Boy cartridge RAM, NES battery saves |

Save states go to `states/` beside the executable and screenshots to
`screenshots/`. Save states, rewind and cheats work on the built-in NES, Game
Boy and CHIP-8 cores; Libretro cores ignore cheats. `rewind_seconds` in
`settings.ini` sets how much history is kept, and 0 turns rewind off.

### Per-game settings

`<game>.ini` overrides the global settings for one game. Only the keys in the
file are applied, so it can change one setting without pinning the rest:

```ini
show_fps=true
integer_scale=false
fast_forward_speed=4
```

Recognised keys are `scale`, `integer_scale`, `bilinear_filter`, `vsync`,
`show_fps`, `mute_audio` and `fast_forward_speed`. The pause menu's "This
game" page edits the same file.

### Cheats

`<game>.cht` holds one NES Game Genie code or raw patch per line, with an
optional description after it:

```text
# Infinite lives
SXIOPO
0100:63     Write 0x63 at 0x0100
8123?A9:EA  Only patch when the byte already reads 0xA9
-0300:01    A leading dash disables a line
```

They load automatically, and F9 toggles them during play.

## Core installer

The library can download PS1, PSP and PS2 cores from an HTTPS server. Cores
run as native code, so every download is checked against a `manifest.txt` of
SHA-256 checksums, and anything that does not match is rejected and deleted.
Without a manifest Toaster installs nothing.

```text
cores/
├── manifest.txt
├── swanstation_libretro.dll
├── ppsspp_libretro.dll
└── pcsx2_libretro.dll
```

Each manifest line is a 64-character checksum followed by a bare filename.
Generate one from the folder holding the DLLs, or from the live URL to hash
what the server is actually serving rather than a local copy that may have
drifted:

```bash
tools/make-core-manifest.sh /path/to/cores
```

```powershell
pwsh tools/make-core-manifest.ps1 -Path C:\cores
pwsh tools/make-core-manifest.ps1 -Url https://your-domain.example/cores
```

Regenerate it whenever a core changes, or the old checksums will reject the
new files. Then start with:

```bash
build/emu.exe --core-server "https://your-domain.example/cores"
```

A packaged install ships with the server already configured: press **Install
Cores** in the library and it fetches everything the PlayStation and PSP cores
need.

`--core-server-unverified` installs without a manifest after a confirmation
prompt. A tampered download would then run as native code with your account's
privileges, so publish a manifest instead. Only serve files you may legally
redistribute, and never put BIOS files or games on a core server.

## Optional library service

A small Python service scans ROM folders into SQLite and adds shared metadata,
favorites, recently played, play counts, tags, statistics and metadata-only
catalog search. See [services/library_server](../services/library_server) for
the API key, the database location and the endpoint list.

```bash
cd services/library_server
python -m pip install -r requirements.txt
python -m uvicorn app:app --host 127.0.0.1 --port 8765
```

```bash
build/emu.exe --games-dir "/path/to/games" \
  --library-server http://127.0.0.1:8765
```

An `https://` URL works too, with certificate verification, so the service
does not have to run on the same machine. Without `--library-server` Toaster
uses its built-in offline scanner. A rescan runs on a background thread, so
the library stays responsive while it works.

Catalog results are metadata only: Toaster launches games solely from local
paths found by the scan, and the service never downloads ROMs.

## Diagnostics

`--verbose` writes launch logging to `logs/toaster.log`. If PS2 shaders fail
to compile, update your graphics driver; that core needs OpenGL 4.3 or newer.
