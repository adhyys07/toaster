# Toaster

Toaster is a small retro-game frontend for:

- NES
- CHIP-8
- Game Boy
- PlayStation 1
- PSP
- PlayStation 2

NES, CHIP-8 and Game Boy run on built-in cores. PS1, PSP, and PS2 support uses
external Libretro cores.

## Run games

### PS1

```bash
build/emu.exe \
  --ps1 "/path/to/game.cue" \
  --system-dir "/path/to/system"
```

Install your own legally dumped BIOS in:

```text
system/swanstation/bios/
```

### PSP

```bash
build/emu.exe \
  --system-dir "/path/to/system" \
  "/path/to/game.iso"
```

PPSSPP assets belong in:

```text
system/PPSSPP/
```

### PS2

Use `--ps2` for ISO files so they are not confused with PSP games:

```bash
build/emu.exe \
  --ps2 "/path/to/game.iso" \
  --system-dir "/path/to/system"
```

Install your own legally dumped BIOS in:

```text
system/pcsx2/bios/
```

BIOS files are private firmware. Toaster does not distribute or upload BIOS
files.

## Controls

Keyboard controls during Libretro gameplay:

| Action | Key |
|---|---|
| D-pad | Arrow keys or numpad 2/4/6/8 |
| Left stick | W/A/S/D |
| Right stick | I/J/K/L |
| Cross, Circle, Square, Triangle | Z, X, C, V |
| L1 / R1 | Q / E |
| L2 / R2 | 1 / 3 |
| Start / Select | Enter / Backspace |
| Pause/configuration menu | F1, then Up/Down and Enter |
| Resume from menu | Escape |
| Fullscreen | F11 |
| Exit | Escape |

Keyboard controls during built-in core gameplay (NES, Game Boy, CHIP-8):

| Action | Key |
|---|---|
| Buttons and D-pad | As configured in Settings → Configure Controller |
| Pause menu | F1, then Up/Down and Enter |
| Close the pause menu | Escape or F1 |
| Save state to slot 1-9 | Shift + F1 - F9 |
| Load state from slot 1-9 | Ctrl + 1 - 9 |
| Rewind | Hold Backspace |
| Pause / frame advance | P / N |
| Fast forward | Tab |
| Volume down / up / mute | - / = / M |
| Toggle cheats | F9 |
| Screenshot | F10 |
| Fullscreen | F11 |
| Register view | F12 |
| Exit | Escape |

F1 opens the pause menu for both built-in and Libretro cores, which is why
saving is on Shift + F1-F9. While the menu is open it takes the keyboard and
the controller, so nothing reaches the game.

Screenshots are written as PNGs to a `screenshots` folder beside the
executable.

### Cheats

Put a `<game>.cht` file next to the ROM. Each line is a NES Game Genie code or
a raw patch, with an optional description after it:

```text
# Infinite lives
SXIOPO
0100:63     Raw patch: write 0x63 at 0x0100
8123?A9:EA  Only patch when the byte already reads 0xA9
-0300:01    A leading dash disables a line
```

Cheats load automatically and F9 toggles them during play. They work on the
NES and Game Boy cores, which can intercept CPU reads; Libretro cores ignore
them.

Save states and rewind work on the NES, Game Boy, and CHIP-8 cores. States are
written to a `states` folder beside the executable, and `rewind_seconds` in
`settings.ini` controls how much history is kept (0 turns rewind off).

Game Boy cartridge RAM is written to `<game>.srm` next to the ROM when the
game exits, so in-game saves survive. NES battery saves use `<game>.sav`.

### Per-game settings

A `<game>.ini` beside a ROM overrides the global settings for that game only.
Only the keys present in the file are applied, so a file can override one
setting without resetting the rest:

```ini
show_fps=true
integer_scale=false
fast_forward_speed=4
```

Recognised keys are `scale`, `integer_scale`, `bilinear_filter`, `vsync`,
`show_fps`, `mute_audio`, and `fast_forward_speed`.

SDL-compatible game controllers are supported.

## Core installer

The Game Library can download cores from an HTTPS server. The server must
contain the permitted core files and a `manifest.txt` listing their SHA-256
checksums:

```text
cores/
├── manifest.txt
├── swanstation_libretro.dll
├── ppsspp_libretro.dll
└── pcsx2_libretro.dll
```

Cores are loaded as native code, so every download is checked against the
manifest and a core that does not match is rejected and deleted. Without a
manifest, Toaster refuses to install anything.

Generate it in the folder holding the DLLs:

```bash
# -t forces text mode. Without it, sha256sum on Windows writes "<hash> *name"
# and the asterisk ends up read as part of the filename.
sha256sum -t *.dll > manifest.txt
```

or, on a Windows server:

```powershell
Get-ChildItem -Filter *.dll | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Name
} | Set-Content -Encoding ascii manifest.txt
```

Each line is a 64-character checksum followed by a bare filename:

```text
023fa2f7ec559e58a1c082a3fa2612f8149d8d9b0657371a69b4fad10f9420db  swanstation_libretro.dll
```

Start Toaster with:

```bash
build/emu.exe \
  --core-server "https://your-domain.example/cores"
```

If you run the server yourself and accept the risk, `--core-server-unverified`
installs without a manifest after a confirmation prompt. A tampered download
would then run as native code with your account's privileges, so prefer
publishing a manifest.

When you install the full version of toaster you already get the server configured you just need to hit Install Cores in emulator and it will install and compile all the cores for all the emulators that needs some additional cores like: PS consoles

Only distribute files you are legally allowed to redistribute. Never place
BIOS files or games on the core server.

## Game library

Start without a game to open the library:

```bash
build/emu.exe
```

Put games in `games/`, or select another folder:

```bash
build/emu.exe --games-dir "/path/to/games"
```

The library supports common NES, Game Boy, PSP, PS1, and PS2 image formats.

### Optional Python library service (refer to that folder for how to fetch an api key and get the game db data)

The optional Python service scans ROM folders into SQLite and provides shared
metadata, favorites, recently played games, play counts, tags, statistics, and
metadata-only catalog search.

```bash
cd services/library_server
python -m pip install -r requirements.txt
python -m uvicorn app:app --host 127.0.0.1 --port 8765
```

Then start Toaster with:

```bash
build/emu.exe --games-dir "/path/to/games" \
  --library-server http://127.0.0.1:8765
```

An `https://` URL works too, with certificate verification enabled, so the
service does not have to run on the same machine.

Without `--library-server`, Toaster continues using its built-in offline
library scanner. A rescan runs on a background thread, so the library window
stays responsive while the service works.

The service provides these endpoints:

| Endpoint | Purpose |
|---|---|
| `GET /health` | Liveness and version |
| `POST /scan` | Scan a local games folder; also prunes rows whose file is gone |
| `GET /games` | List locally available games; takes `limit` and `offset` |
| `GET /games?filter=favorites` | List favorite games |
| `GET /games?filter=recent` | List recently played games, newest first |
| `GET /games/{id}` | One game by id |
| `POST /games/{id}/favorite` | Change a favorite flag |
| `POST /games/{id}/played` | Record a launch and increment play count |
| `PUT /games/{id}/tags` | Store user tags, normalised and deduplicated |
| `GET /statistics` | Return library totals and system counts |
| `GET /catalog/search?q=...` | Search metadata without downloading games |

`POST /scan` accepts `{"folder": "...", "prune_missing": false}` to keep rows
for files that are no longer present.

Run the tests with:

```bash
cd services/library_server
python -m pip install -r requirements-dev.txt
python -m pytest .
```

The SQLite path defaults to `toaster-library.sqlite`. Set
`TOASTER_DATABASE` to use another location:

```powershell
$env:TOASTER_DATABASE = "D:\toaster\data\library.sqlite"
python -m uvicorn app:app --host 127.0.0.1 --port 8765
```

Catalog search uses TheGamesDB and requires an API key supplied through the
environment:

```powershell
$env:THEGAMESDB_API_KEY = "your-key"
```

Catalog results are metadata only. Toaster launches games only from local
paths returned by the library scan; the service does not download ROMs.

## Why I built this project
I built this project because I feel frustrating to install multiple emulators to run another my favorite game because I cannot play all games in one emulator, to tackle this I built toaster which compiler 5 emulators inside one window.

## Build on Windows

Install CMake, Ninja, MinGW-w64, Python 3, SDL2, SDL2_ttf, SDL2_image, and
libcurl. Then run:

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the executable:

```bash
build/emu.exe
```

## Build on Linux

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build python3 \
  libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
  libcurl4-openssl-dev libgl-dev
```

Build and test:

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux Libretro cores use the `.so` extension instead of `.dll`.

## Diagnostics

Enable launch logging with:

```bash
build/emu.exe --verbose
```

Logs are written to `logs/toaster.log`.

If PS2 shaders fail to compile, update your graphics driver and ensure the
system supports OpenGL 4.3 or newer.

## Legal notice

Toaster does not include games, BIOS files, firmware, or third-party cores.
Use only backups and software you are legally permitted to use.
