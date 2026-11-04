# UI fonts

Drop a `.ttf` here to control the font the library and settings windows use.
`openUiFont()` in `src/frontend/ui_font.cpp` tries, in order:

1. `assets/fonts/Inter-Regular.ttf` beside the executable
2. `assets/fonts/DejaVuSans.ttf` beside the executable
3. the same two paths relative to the working directory
4. platform system fonts (Segoe UI on Windows, SF/Helvetica on macOS,
   DejaVu/Liberation/Noto/FreeSans on Linux)

Bundling a font here makes the UI look identical on every platform and lets it
work on a Linux install that ships no system fonts. Nothing is bundled by
default, because font files have their own licences: add one whose licence
permits redistribution (DejaVu and Inter both do) and commit it alongside this
file.
