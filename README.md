# Toaster

Toaster is a small retro-game frontend for:

- NES
- CHIP-8
- Game Boy
- PlayStation 1
- PSP
- PlayStation 2

PS1, PSP, and PS2 support uses external Libretro cores.

## Quick start

Put cores beside the executable:

```text
Toaster/
├── emu.exe
├── cores/
│   ├── swanstation_libretro.dll
│   ├── ppsspp_libretro.dll
│   └── pcsx2_libretro.dll
└── system/
```

Toaster automatically searches the `cores` folder. You can also specify a core
manually with `--ps1-core`, `--ppsspp-core`, or `--ps2-core`.

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

SDL-compatible game controllers are supported.

## Core installer

The Game Library can download cores from an HTTPS server. The server must
contain permitted core files in `/cores/`:

```text
swanstation_libretro.dll
ppsspp_libretro.dll
pcsx2_libretro.dll
```

Start Toaster with:

```bash
build/emu.exe \
  --core-server "https://your-domain.example/cores"
```

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

### Optional Python library service

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

Without `--library-server`, Toaster continues using its built-in offline
library scanner.

The service provides these endpoints:

| Endpoint | Purpose |
|---|---|
| `POST /scan` | Scan a local games folder |
| `GET /games` | List locally available games |
| `GET /games?filter=favorites` | List favorite games |
| `GET /games?filter=recent` | List recently played games |
| `POST /games/{id}/favorite` | Change a favorite flag |
| `POST /games/{id}/played` | Record a launch and increment play count |
| `PUT /games/{id}/tags` | Store user tags |
| `GET /statistics` | Return library totals and system counts |
| `GET /catalog/search?q=...` | Search metadata without downloading games |

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

## Build on Windows

Install CMake, Ninja, MinGW-w64, Python 3, SDL2, and SDL2_ttf. Then run:

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
  libsdl2-dev libsdl2-ttf-dev libgl-dev
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
