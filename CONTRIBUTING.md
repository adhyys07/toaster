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

Dependencies: CMake 3.21 or newer, Ninja, a C++17 compiler, Python 3, SDL2,
SDL2_ttf, SDL2_image, and libcurl.

The build tree does not contain the runtime DLLs, so `build/emu.exe` only
runs from a shell that has the toolchain on PATH — the normal case inside an
MSYS2 MinGW shell. Launching it from Explorer fails with "SDL2.dll was not
found", because Windows resolves imports before `main()` runs.

For a double-clickable tree with every DLL beside the executable:

```bash
cmake --build build --target run-tree    # writes build/run
```

The DLLs cannot simply be copied next to `build/emu.exe`: the packaging scan
searches the executable's own directory first, so each one would resolve from
both there and the toolchain, and the install fails with "Multiple conflicting
paths found".

## Packaging

```bash
cd build && cpack -G ZIP
```

That is the only packaging path, and it is what CI publishes. On Windows it
writes `toaster-windows-x64.zip` with the executable, its assets and every
DLL it needs at the archive root -- around fifty files, because SDL2_ttf
brings in freetype and harfbuzz, SDL2_image brings in a dozen image codecs,
and libcurl brings in OpenSSL.

Never maintain that DLL list by hand. `install(RUNTIME_DEPENDENCY_SET)` in
`CMakeLists.txt` discovers it, because a hand-written list goes stale on the
next dependency change: v0.1.0 shipped missing the MinGW runtime and would
not start on a machine without the toolchain installed.

The tagged CI build extracts the archive and runs `emu.exe --help` with the
toolchain stripped off PATH. Windows binds imports before `main()` runs, so a
zero exit proves nothing is missing. If you change a dependency, that step is
what will tell you.

## Checking the UI actually draws

Starting is not the same as working. v0.2.0 started cleanly and then showed a
black window, because the library menu drew its whole frame and never called
`SDL_RenderPresent`. Every build and every test still passed: `--help` returns
before a window exists, and nothing in the suite renders a frame.

So before releasing, run the real UI:

```bash
cmake --build build --target run-tree
pwsh tools/verify-window.ps1 -Exe build/run/emu.exe -CleanPath
```

It opens the window, captures it and counts distinct colours. A frame that was
never presented is a single flat colour; the library is around a hundred. The
tagged CI build runs the same script against the extracted archive.

If you touch the render loop, check that the script still fails when it should
-- delete the `SDL_RenderPresent` call, rebuild, and confirm it reports one
colour.

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
