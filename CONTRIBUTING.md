# Contributing to Toaster

## Never commit

- ROMs, disc images, or save files
- BIOS dumps or any console firmware
- Libretro cores or other prebuilt binaries

`.gitignore` covers the usual extensions, but check `git status` before
committing. Toaster distributes none of these and neither should a patch.

## Building

```bash
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default build type is `RelWithDebInfo`. Do not configure without one: an
unoptimized build runs the cycle-accurate cores roughly forty times slower,
which is far too slow to play.

Dependencies: CMake, Ninja, a C++17 compiler, Python 3, SDL2, SDL2_ttf,
SDL2_image, and libcurl.

For the Python library service:

```bash
cd services/library_server
python -m pip install -r requirements-dev.txt
python -m pytest .
```

## Tests

Every core change needs a test. The suites are:

| Target | Covers |
|---|---|
| `core_tests` | The shell interface and save states for every built-in core |
| `gb_tests` | Game Boy CPU, timer, PPU and cartridge behaviour in detail |
| `binding_tests` | Button binding tables |
| `settings_tests` | Settings and per-game override files |
| `cheat_tests` | Game Genie decoding and cheat files |
| `pytest` | Every library service endpoint |

Use a plain failure counter rather than `assert`. The default build defines
`NDEBUG`, which compiles assertions out and leaves a test passing without
testing anything.

## Style

`.clang-format` holds the rules: four spaces, 80 columns, pointers bound to the
type. Comments should say why the code is the way it is, not restate what the
next line does.
