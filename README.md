## Toaster

Toaster is a retro game emulator with NES, CHIP-8, GAME BOY, PS1, PSP, and PS2
frontends through native and Libretro cores.

### Core installer server

The Game Library has an **Install Cores** button. It downloads Windows x64
core DLLs from a static HTTPS directory into the executable's `cores` folder.
The default URL is a placeholder, so configure your server URL when launching:

```text
build/emu.exe --core-server "https://downloads.example.com/toaster/cores"
```

The server directory must contain these exact files:

```text
swanstation_libretro.dll
ppsspp_libretro.dll
pcsx2_libretro.dll
```

For example, with nginx, place the files in
`/var/www/toaster/cores/`, enable HTTPS, and verify:

```text
https://downloads.example.com/toaster/cores/swanstation_libretro.dll
```

The installer downloads emulator cores only. BIOS files are not downloaded
from a server; import legally dumped BIOS files locally using the PS1/PS2 BIOS
buttons in the Game Library.

### PS1 setup

PS1 support uses an external Libretro core such as SwanStation or Beetle PSX.
The core is not bundled. Place it at
`cores/swanstation_libretro.dll` or pass its path with `--ps1-core`.
Place a legally dumped PS1 BIOS in `system/swanstation/bios`.

Launch a PS1 game explicitly:

```text
build/emu.exe --ps1-core "D:/toaster/cores/swanstation_libretro.dll" --system-dir "D:/toaster/system" --ps1 "D:/My Games/game.cue"
```

`.cue`, `.chd`, and `.pbp` images may be supported by the selected core.
Use a `.cue` file together with its referenced `.bin` tracks when possible.

### PSP setup

The PPSSPP core and its data assets are separate downloads and are not included
with Toaster. Put the core in `cores/ppsspp_libretro.dll` (or pass its path with
`--ppsspp-core`). Place PPSSPP's required assets in `system/PPSSPP` and pass the
parent `system` directory with `--system-dir` if it is elsewhere. The PPSSPP
core requires an OpenGL-capable system and its assets, including `lang` and
`flash0` data.

Start a PSP game from the command line:

```text
build/emu.exe --ppsspp-core "D:/path/to/ppsspp_libretro.dll" --system-dir "D:/toaster/system" "D:/path/to/game.iso"
```

The game library also lists PSP images with `.iso`, `.cso`, `.pbp`, `.chd`,
`.elf`, and `.prx` extensions. Keyboard controls are arrow keys for the D-pad,
Z/X/A/S for Cross/Circle/Square/Triangle, Q/W for L/R, Enter for Start, and
Right Shift for Select. A connected SDL game controller is supported as well.
Press F11 to toggle fullscreen and Escape to exit gameplay.

### PS2 setup (LRPS2)

PS2 support loads the LRPS2 Libretro core into Toaster's existing Libretro/OpenGL
host. The core is not bundled. Place its Windows x64 build at
`cores/pcsx2_libretro.dll`, or pass a different path with `--ps2-core`.
LRPS2 requires a PS2 BIOS dumped from a console you own. Put the extracted BIOS
files under `system/pcsx2/bios`; see the
[LRPS2 setup guide](https://docs.libretro.com/library/lrps2/) for required BIOS
files and hardware requirements.

Launch a PS2 disc image explicitly with `--ps2` so `.iso` files are not confused
with PSP games:

```text
build/emu.exe --ps2-core "D:/toaster/cores/pcsx2_libretro.dll" --system-dir "D:/toaster/system" --ps2 "D:/My Games/game.iso"
```

From the game library, shared formats such as `.iso`, `.cso`, `.chd`, and `.elf`
prompt you to choose PSP or PS2. PS2-only formats are routed to LRPS2.

### Build, test, and package

Configure and build with CMake:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

The package target creates a portable ZIP containing the emulator, shaders, and
README. Continuous integration runs the build and tests on Windows and Linux.
