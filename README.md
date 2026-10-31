## Toaster

Toaster is a retro game emulator with NES and CHIP-8 cores and an optional PSP
frontend through PPSSPP's Libretro core.

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
