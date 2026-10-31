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
