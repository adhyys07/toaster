#define SDL_MAIN_HANDLED
#include "hotkeys.h"
#include "save_state_manager.h"
#include "ui/controller_view.h"
#include "ui/game_overlay.h"
#include "ui/game_view.h"
#include "ui/layout.h"
#include "ui/text_cache.h"
#include "ui/theme.h"
#include "ui/theme_io.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL  %s\n", what);
        ++failures;
    }
}

void check(bool condition, const std::string& what) {
    check(condition, what.c_str());
}

void checkEqual(int actual, int expected, const char* what) {
    if (actual != expected) {
        std::printf("FAIL  %s (expected %d, got %d)\n", what, expected, actual);
        ++failures;
    }
}

void checkEqual(const std::string& actual, const std::string& expected,
                const char* what) {
    if (actual != expected) {
        std::printf("FAIL  %s (expected \"%s\", got \"%s\")\n", what,
                    expected.c_str(), actual.c_str());
        ++failures;
    }
}

using namespace emu::frontend::ui;


void testFuzzyMatch() {
    int score = 0;

    check(fuzzyMatch("final fantasy vii", "ffvii", &score),
          "an acronym matches across words");
    check(score > 0, "a match scores above zero");

    check(fuzzyMatch("final fantasy vii", "", &score),
          "an empty query matches everything");
    checkEqual(score, 0, "an empty query scores zero");

    check(!fuzzyMatch("final fantasy vii", "xyz", &score),
          "letters that are not there do not match");
    check(!fuzzyMatch("zelda", "zeldaii", &score),
          "a query longer than the title cannot match");
    check(!fuzzyMatch("abc", "cba", &score),
          "order matters: a subsequence is not an anagram");

    check(fuzzyMatch("super mario bros", "smb", &score),
          "initials match");
    check(fuzzyMatch("super mario bros", "mario", &score),
          "a whole word matches");

    int leading = 0;
    int scattered = 0;
    check(fuzzyMatch("metroid", "met", &leading), "leading match found");
    check(fuzzyMatch("the maze of eternity", "met", &scattered),
          "scattered match found");
    check(leading > scattered,
          "a leading match outranks a scattered one");

    int shorter = 0;
    int longer = 0;
    check(fuzzyMatch("zelda", "zelda", &shorter), "exact short match found");
    check(fuzzyMatch("zelda ii the adventure of link", "zelda", &longer),
          "exact prefix of a long title found");
    check(shorter > longer, "the shorter title wins a tie");

    check(fuzzyMatch("aaa", "aa", &score), "repeated letters match");
}

void testFoldCase() {
    checkEqual(foldCase("Final Fantasy VII"), "final fantasy vii",
               "foldCase lowercases");
    checkEqual(foldCase(""), "", "foldCase handles an empty string");
    checkEqual(foldCase("123-ABC_d"), "123-abc_d",
               "foldCase leaves punctuation and digits alone");
}


void testSystemFromPath() {
    check(systemFromPath("a/b/game.nes") == SystemId::Nes, ".nes is NES");
    check(systemFromPath("game.gb") == SystemId::GameBoy, ".gb is Game Boy");
    check(systemFromPath("game.gbc") == SystemId::GameBoy, ".gbc is Game Boy");
    check(systemFromPath("game.ch8") == SystemId::Chip8, ".ch8 is CHIP-8");
    check(systemFromPath("game.cue") == SystemId::Ps1, ".cue is PS1");
    check(systemFromPath("game.chd") == SystemId::Ps1, ".chd is PS1");
    check(systemFromPath("game.pbp") == SystemId::Ps1, ".pbp is PS1");
    check(systemFromPath("game.m3u") == SystemId::Ps1, ".m3u is PS1");
    check(systemFromPath("game.bin") == SystemId::Ps1, ".bin is PS1");
    check(systemFromPath("game.cso") == SystemId::Psp, ".cso is PSP");

    check(systemFromPath("game.iso") == SystemId::Psp,
          ".iso resolves to PSP, matching the launcher default");

    check(systemFromPath("GAME.NES") == SystemId::Nes,
          "the extension test is case insensitive");
    check(systemFromPath("readme.txt") == SystemId::Unknown,
          "an unrelated extension is Unknown");
    check(systemFromPath("noextension") == SystemId::Unknown,
          "a file with no extension is Unknown");

    for (const SystemId system : {SystemId::Unknown, SystemId::Nes,
                                  SystemId::GameBoy, SystemId::Chip8,
                                  SystemId::Ps1, SystemId::Psp,
                                  SystemId::Ps2}) {
        const char* name = systemShortName(system);
        check(name != nullptr && name[0] != '\0',
              "every system has a short name");
    }
}


struct FakeGame {
    std::filesystem::path path;
    std::string title;
    std::string year = "Unknown";
    bool favorite = false;
    // GameViewT reads this for the Recent filter and the LastPlayed sort, so
    // the stand-in has to carry it too.
    long long last_played = 0;
};

void testGameView() {
    std::vector<FakeGame> games = {
        {"games/zelda.nes", "The Legend of Zelda", "1986", false},
        {"games/metroid.nes", "Metroid", "1986", true},
        {"games/ff7.cue", "Final Fantasy VII", "1997", false},
        {"games/tetris.gb", "Tetris", "1989", true},
    };

    GameViewT<FakeGame> view;
    view.setSource(&games);

    check(view.refresh(), "the first refresh rebuilds");
    checkEqual(static_cast<int>(view.size()), 4, "every game is visible");
    check(!view.refresh(), "a second refresh with no change does nothing");

    checkEqual(view.at(0).title, "Final Fantasy VII", "sorted by title");
    checkEqual(view.at(3).title, "The Legend of Zelda", "sorted by title");

    view.setFilter(GameViewT<FakeGame>::Filter::Favorites);
    check(view.refresh(), "changing the filter rebuilds");
    checkEqual(static_cast<int>(view.size()), 2, "two games are favorites");
    check(view.at(0).favorite && view.at(1).favorite,
          "only favorites survive the filter");

    view.setFilter(GameViewT<FakeGame>::Filter::System);
    view.setSystem(SystemId::Nes);
    check(view.refresh(), "changing the system rebuilds");
    checkEqual(static_cast<int>(view.size()), 2, "two NES games");

    view.setFilter(GameViewT<FakeGame>::Filter::All);
    view.refresh();

    view.setQuery("ffvii");
    check(view.refresh(), "a query rebuilds");
    checkEqual(static_cast<int>(view.size()), 1, "the acronym finds one game");
    checkEqual(view.at(0).title, "Final Fantasy VII",
               "the acronym finds the right game");

    view.setQuery("zzzz");
    view.refresh();
    check(view.empty(), "an unmatched query leaves nothing");

    view.setQuery("");
    view.refresh();
    checkEqual(static_cast<int>(view.size()), 4, "clearing the query restores");

    games[0].favorite = true;
    check(!view.refresh(), "an in-place change is invisible until invalidated");
    view.invalidate();
    check(view.refresh(), "invalidate forces a rebuild");

    const long long index = view.indexOfPath("games/tetris.gb");
    check(index >= 0, "a visible path is found");
    checkEqual(view.at(static_cast<std::size_t>(index)).title, "Tetris",
               "indexOfPath returns the right row");

    view.setFilter(GameViewT<FakeGame>::Filter::System);
    view.setSystem(SystemId::Ps1);
    view.refresh();
    checkEqual(static_cast<int>(view.indexOfPath("games/tetris.gb")), -1,
               "a filtered-out path reports -1");

    std::vector<FakeGame> empty;
    GameViewT<FakeGame> empty_view;
    empty_view.setSource(&empty);
    empty_view.refresh();
    check(empty_view.empty(), "an empty library yields an empty view");
    checkEqual(static_cast<int>(empty_view.indexOfPath("anything")), -1,
               "indexOfPath on an empty view reports -1");
}


bool rectInside(const SDL_Rect& inner, const SDL_Rect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

bool overlaps(const SDL_Rect& a, const SDL_Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
           b.y < a.y + a.h;
}

void checkNoNegatives(const LibraryLayout& l, const char* context) {
    const SDL_Rect rects[] = {l.window,    l.header, l.list_panel,
                              l.detail_panel, l.search, l.chips,
                              l.rows,      l.scrollbar, l.art,
                              l.detail_body, l.footer};
    for (const SDL_Rect& rect : rects) {
        if (rect.w < 0 || rect.h < 0) {
            std::printf("FAIL  %s: a rect has negative size (%d x %d)\n",
                        context, rect.w, rect.h);
            ++failures;
            return;
        }
    }
}

void testLayout() {
    Metrics metrics;

    metrics.scale = 1.0f;
    LibraryLayout tiny = layoutLibrary(320, 240, metrics, ViewMode::List);
    checkNoNegatives(tiny, "320x240");
    check(!tiny.two_pane, "a tiny window drops the detail pane");
    check(tiny.visible_rows >= 0, "visible_rows is never negative");

    LibraryLayout narrow = layoutLibrary(880, 700, metrics, ViewMode::List);
    checkNoNegatives(narrow, "880x700");
    check(!narrow.two_pane, "below 900px the detail pane is dropped");

    LibraryLayout wide = layoutLibrary(1180, 740, metrics, ViewMode::List);
    checkNoNegatives(wide, "1180x740");
    check(wide.two_pane, "a wide window keeps both panes");
    check(!overlaps(wide.list_panel, wide.detail_panel),
          "the two panes do not overlap");
    check(rectInside(wide.search, wide.list_panel),
          "the search field sits inside the list pane");
    check(rectInside(wide.chips, wide.list_panel),
          "the chips sit inside the list pane");
    check(rectInside(wide.rows, wide.list_panel),
          "the rows sit inside the list pane");
    check(rectInside(wide.scrollbar, wide.rows),
          "the scrollbar sits inside the rows region");
    check(wide.scrollbar.w > 0, "the scrollbar has a width");
    check(rectInside(wide.art, wide.detail_panel),
          "the cover sits inside the detail pane");
    check(rectInside(wide.detail_body, wide.detail_panel),
          "the detail body sits inside the detail pane");
    check(!overlaps(wide.header, wide.list_panel),
          "the header does not overlap the list");
    check(!overlaps(wide.footer, wide.list_panel),
          "the footer does not overlap the list");

    LibraryLayout tall = layoutLibrary(1180, 1080, metrics, ViewMode::List);
    checkNoNegatives(tall, "1180x1080");
    check(tall.visible_rows > wide.visible_rows,
          "a taller window shows more rows");

    LibraryLayout huge = layoutLibrary(1920, 1080, metrics, ViewMode::List);
    checkNoNegatives(huge, "1920x1080");
    check(!overlaps(huge.list_panel, huge.detail_panel),
          "the panes do not overlap at 1920x1080");
    check(huge.list_panel.w > wide.list_panel.w,
          "the list pane grows with the window");

    metrics.scale = 2.0f;
    LibraryLayout scaled = layoutLibrary(1180, 740, metrics, ViewMode::List);
    checkNoNegatives(scaled, "1180x740 at 2x");
    check(!scaled.two_pane,
          "at 2x a 1180px window is below the two-pane threshold");
    check(scaled.row_height > wide.row_height,
          "rows are taller at 2x");

    LibraryLayout scaled_wide =
        layoutLibrary(2360, 1480, metrics, ViewMode::List);
    checkNoNegatives(scaled_wide, "2360x1480 at 2x");
    check(scaled_wide.two_pane, "twice the window at 2x keeps both panes");
    check(!overlaps(scaled_wide.list_panel, scaled_wide.detail_panel),
          "the panes do not overlap at 2x");

    metrics.scale = 1.0f;
    LibraryLayout grid = layoutLibrary(1180, 740, metrics, ViewMode::Grid);
    checkNoNegatives(grid, "1180x740 grid");
    check(!grid.two_pane, "grid mode uses the whole window");
    check(grid.columns >= 1, "grid mode has at least one column");
    check(grid.cell_w > 0 && grid.cell_h > 0, "grid cells have a size");
    check(grid.columns * grid.cell_w <= grid.rows.w,
          "the columns fit inside the rows region");

    LibraryLayout narrow_grid = layoutLibrary(300, 400, metrics, ViewMode::Grid);
    checkNoNegatives(narrow_grid, "300x400 grid");
    check(narrow_grid.columns >= 1,
          "even a very narrow window has one column");

    const SDL_Rect row0 = rowRect(wide, 0, 0);
    const SDL_Rect row1 = rowRect(wide, 1, 0);
    checkEqual(row1.y - row0.y, wide.row_height, "rows step by row_height");
    check(row0.y == wide.rows.y, "the first row starts at the top");
    check(row0.w > 0, "a row has a width");
    check(row0.x + row0.w <= wide.scrollbar.x,
          "a row stops before the scrollbar");

    const SDL_Rect scrolled = rowRect(wide, 0, 50);
    checkEqual(scrolled.y, row0.y - 50, "scrolling shifts rows up");

    const SDL_Rect cell0 = cellRect(grid, 0, 0);
    const SDL_Rect cell_next_column = cellRect(grid, 1, 0);
    check(cell_next_column.x > cell0.x,
          "the next cell is to the right");
    if (grid.columns >= 1) {
        const SDL_Rect cell_next_row = cellRect(grid, grid.columns, 0);
        checkEqual(cell_next_row.x, cell0.x,
                   "the row wraps back to the first column");
        check(cell_next_row.y > cell0.y, "the next row is below");
    }

    const SDL_Rect none = cellRect(wide, 0, 0);
    check(none.w == 0 && none.h == 0,
          "cellRect on a list layout returns an empty rect");
}

void testToRenderCoords() {
    int x = 100;
    int y = 50;
    toRenderCoords(1.0f, &x, &y);
    checkEqual(x, 100, "1x leaves x alone");
    checkEqual(y, 50, "1x leaves y alone");

    x = 100;
    y = 50;
    toRenderCoords(1.5f, &x, &y);
    checkEqual(x, 150, "1.5x scales x");
    checkEqual(y, 75, "1.5x scales y");

    x = 10;
    toRenderCoords(2.0f, &x, nullptr);
    checkEqual(x, 20, "a null y is ignored");
    toRenderCoords(2.0f, nullptr, nullptr);
}


bool endsMidCodePoint(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((byte & 0x80) == 0x00) length = 1;
        else if ((byte & 0xE0) == 0xC0) length = 2;
        else if ((byte & 0xF0) == 0xE0) length = 3;
        else if ((byte & 0xF8) == 0xF0) length = 4;
        else return true;   // a bare continuation byte

        if (i + length > text.size()) return true;
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return true;
            }
        }
        i += length;
    }
    return false;
}

void testColorParsing() {
      SDL_Color color{};

      check(parseColor("#5ec8e1", color), "a six-digit hex parses");
      checkEqual(color.r, 0x5E, "red");
      checkEqual(color.g, 0xC8, "green");
      checkEqual(color.b, 0xE1, "blue");
      checkEqual(color.a, 255, "alpha defaults to opaque");

      check(parseColor("5ec8e1", color), "the hash is optional");
      check(parseColor("#5EC8E1", color), "upper case parses");
      checkEqual(color.r, 0x5E, "case does not change the value");

      check(parseColor("#abc", color), "a three-digit hex parses");
      checkEqual(color.r, 0xAA, "#abc expands each digit");
      checkEqual(color.b, 0xCC, "#abc expands each digit");

      check(parseColor("#0000007f", color), "an eight-digit hex parses");
      checkEqual(color.a, 0x7F, "alpha is read");

      check(!parseColor("", color), "an empty string is not a colour");
      check(!parseColor("#12345", color), "five digits is not a colour");
      check(!parseColor("#gggggg", color), "non-hex is not a colour");
      check(!parseColor("blue", color), "a name is not a colour");

      for (int i = 0; i < kThemeFieldCount; ++i) {
            const Theme theme = themePreset(ThemeId::Nord);
            const SDL_Color original = theme.*(kThemeFields[i].member);
            SDL_Color returned{};
            check(parseColor(formatColor(original), returned),
              "a formatted colour parses back");
            check(original.r == returned.r && original.g == returned.g &&
                  original.b == returned.b && original.a == returned.a,
              "format and parse round-trip exactly");
      }
}

void testThemePresets() {
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const auto id = static_cast<ThemeId>(i);
        check(themeFromName(themeName(id)) == id,
              "every preset name round-trips");

        if (id == ThemeId::Custom) continue;
        const std::vector<ThemeProblem> problems =
            validateTheme(themePreset(id));
        for (const ThemeProblem& problem : problems) {
            std::printf("FAIL  preset %s: %s\n", themeName(id),
                        problem.message.c_str());
            ++failures;
        }
    }

    check(themeFromName("nonsense") == ThemeId::Midnight,
          "an unknown name falls back to the default");
    check(themeFromName("") == ThemeId::Midnight,
          "an empty name falls back to the default");
}

void testContrastAndRepair() {
    const SDL_Color white{255, 255, 255, 255};
    const SDL_Color black{0, 0, 0, 255};

    check(contrastRatio(white, black) > 20.0f, "black on white is maximal");
    check(contrastRatio(white, white) < 1.01f, "a colour on itself is 1:1");
    check(contrastRatio(white, black) == contrastRatio(black, white),
          "contrast is symmetric");

    Theme broken;
    broken.bg = black;
    broken.surface = black;
    broken.text = black;
    broken.text_muted = black;
    broken.border = black;
    broken.accent = black;
    broken.text_on_accent = black;
    broken.gold = black;

    const Theme repaired = repairTheme(broken);
    check(contrastRatio(repaired.text, repaired.surface) >= 1.6f,
          "repair makes text legible on a panel");
    check(contrastRatio(repaired.text, repaired.bg) >= 1.6f,
          "repair makes text legible on the window");
    check(contrastRatio(repaired.text_on_accent, repaired.accent) >= 1.6f,
          "repair makes accent text legible");
    check(contrastRatio(repaired.border, repaired.surface) > 1.0f,
          "repair makes borders visible");

    // Repair must leave a good theme completely alone.
    const Theme good = themePreset(ThemeId::Nord);
    const Theme untouched = repairTheme(good);
    check(untouched.text.r == good.text.r && untouched.bg.r == good.bg.r &&
              untouched.accent.g == good.accent.g,
          "repair does not alter a legible theme");
}

void testThemeFile() {
    const std::filesystem::path path = "test-theme.ini";
    std::filesystem::remove(path);

    const Theme original = themePreset(ThemeId::Gruvbox);
    std::string error;
    check(saveTheme(path, original, error), "a theme writes");

    Theme loaded = themePreset(ThemeId::Midnight);
    std::vector<std::string> warnings;
    check(loadTheme(path, loaded, warnings, error), "a theme reads back");
    check(warnings.empty(), "a theme we wrote loads without warnings");
    check(loaded.bg.r == original.bg.r && loaded.accent.b == original.accent.b,
          "the theme survives a round trip");

    // A partial file applies only what it names, so a user can override one
    // colour without restating the other thirteen.
    {
        std::ofstream partial_file("test-theme-partial.ini");
        partial_file << "# just the accent\naccent=#ff0000\n";
    }
    Theme partial = themePreset(ThemeId::Nord);
    const SDL_Color nord_bg = partial.bg;
    warnings.clear();
    check(loadTheme("test-theme-partial.ini", partial, warnings, error),
          "a partial theme loads");
    checkEqual(partial.accent.r, 255, "the named colour is applied");
    checkEqual(partial.bg.r, nord_bg.r, "unnamed colours are left alone");

    // Garbage must be survivable: warn, do not fail.
    {
        std::ofstream messy_file("test-theme-messy.ini");
        messy_file << "accent=#00ff00\n"
                      "not_a_colour=#123456\n"
                      "bg=orange\n"
                      "no equals sign here\n";
    }
    Theme messy = themePreset(ThemeId::Midnight);
    warnings.clear();
    check(loadTheme("test-theme-messy.ini", messy, warnings, error),
          "a partly broken theme still loads");
    checkEqual(messy.accent.g, 255, "the valid line is applied");
    checkEqual(static_cast<int>(warnings.size()), 3,
               "each bad line warns exactly once");

    // A file with nothing usable is an error, not a silent no-op: otherwise a
    // typo in every line looks like a theme that simply did nothing.
    {
        std::ofstream empty_file("test-theme-empty.ini");
        empty_file << "# only a comment\n";
    }
    Theme untouched = themePreset(ThemeId::Midnight);
    warnings.clear();
    check(!loadTheme("test-theme-empty.ini", untouched, warnings, error),
          "a theme that sets nothing is an error");
    check(!error.empty(), "that error says something");

    check(!loadTheme("does-not-exist.ini", untouched, warnings, error),
          "a missing file is an error");

    for (const char* name : {"test-theme.ini", "test-theme-partial.ini",
                             "test-theme-messy.ini", "test-theme-empty.ini"}) {
        std::filesystem::remove(name);
    }
}

void testControllerLayout() {
    const ControllerGeometry g = layoutController({0, 0, 400, 400});

    check(g.body.w > 0 && g.body.h > 0, "the body has a size");
    // The diagram must keep its proportion rather than stretch to the box.
    const float ratio = static_cast<float>(g.body.h) / g.body.w;
    check(ratio > 0.55f && ratio < 0.70f,
          "the body keeps a controller's proportion in a square box");
    check(g.body.x >= 0 && g.body.y >= 0, "the body stays inside the bounds");

    const ControllerGeometry wide = layoutController({0, 0, 1000, 200});
    check(wide.body.h <= 200, "a wide box is limited by its height");
    check(wide.body.w <= 1000, "a wide box is limited by its width");

    // Every control the diagram draws must resolve to a real rect, or it is
    // drawn at the origin where nobody will find it.
    const SDL_GameControllerButton drawn[] = {
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_LEFTSTICK,
        SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_GUIDE};

    for (const SDL_GameControllerButton button : drawn) {
        const SDL_Rect rect = controllerButtonRect(g, button);
        if (rect.w <= 0 || rect.h <= 0) {
            std::printf("FAIL  button %d has no rect\n",
                        static_cast<int>(button));
            ++failures;
        }
    }

    checkEqual(controllerButtonRect(g, SDL_CONTROLLER_BUTTON_INVALID).w, 0,
               "an invalid button has no rect");

    // The face buttons must be arranged as a diamond, not stacked.
    const SDL_Rect a = controllerButtonRect(g, SDL_CONTROLLER_BUTTON_A);
    const SDL_Rect b = controllerButtonRect(g, SDL_CONTROLLER_BUTTON_B);
    const SDL_Rect x = controllerButtonRect(g, SDL_CONTROLLER_BUTTON_X);
    const SDL_Rect y = controllerButtonRect(g, SDL_CONTROLLER_BUTTON_Y);
    check(a.y > y.y, "A sits below Y");
    check(b.x > x.x, "B sits right of X");

    // The d-pad arms must not overlap each other.
    const SDL_Rect up = controllerButtonRect(g, SDL_CONTROLLER_BUTTON_DPAD_UP);
    const SDL_Rect down =
        controllerButtonRect(g, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    const SDL_Rect left =
        controllerButtonRect(g, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    const SDL_Rect right =
        controllerButtonRect(g, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    check(up.y + up.h <= down.y, "the d-pad arms do not overlap vertically");
    check(left.x + left.w <= right.x,
          "the d-pad arms do not overlap horizontally");

    // The two sticks must be distinct controls, not drawn on top of each
    // other, which a sign error in the layout table would produce.
    check(g.left_stick.x != g.right_stick.x ||
              g.left_stick.y != g.right_stick.y,
          "the sticks are in different places");
    check(g.left_shoulder.x < g.right_shoulder.x,
          "the shoulders are the right way round");
    check(g.left_trigger.y < g.left_shoulder.y,
          "the triggers sit above the shoulders");

    // No two clusters may touch. Every version of these proportions so far
    // has had some pair overlapping, and it is invisible until the diagram
    // is on screen, so it is asserted here.
    const struct {
        const char* name;
        SDL_Rect rect;
    } clusters[] = {
        {"left stick", g.left_stick},   {"right stick", g.right_stick},
        {"d-pad", g.dpad},              {"face buttons", g.face},
        {"left shoulder", g.left_shoulder},
        {"right shoulder", g.right_shoulder},
        {"left trigger", g.left_trigger},
        {"right trigger", g.right_trigger},
    };
    const int cluster_count =
        static_cast<int>(sizeof(clusters) / sizeof(clusters[0]));

    for (int i = 0; i < cluster_count; ++i) {
        for (int k = i + 1; k < cluster_count; ++k) {
            if (overlaps(clusters[i].rect, clusters[k].rect)) {
                std::printf("FAIL  %s overlaps %s\n", clusters[i].name,
                            clusters[k].name);
                ++failures;
            }
        }
    }

    // Everything the diagram draws has to stay inside the rect it was given,
    // including the triggers, which sit above the body and used to be drawn
    // over whatever was behind the widget.
    const SDL_Rect bounds{40, 60, 500, 400};
    const ControllerGeometry fitted = layoutController(bounds);
    const SDL_Rect parts[] = {
        fitted.body,           fitted.left_trigger, fitted.right_trigger,
        fitted.left_shoulder,  fitted.right_shoulder};
    for (const SDL_Rect& part : parts) {
        if (!rectInside(part, bounds)) {
            std::printf("FAIL  a control escapes the bounds: %d,%d %dx%d\n",
                        part.x, part.y, part.w, part.h);
            ++failures;
        }
    }

    // The caption is drawn below the body, so there must be room left for it
    // inside the bounds.
    check(fitted.body.y + fitted.body.h < bounds.y + bounds.h,
          "the body leaves room for the caption");

    // A degenerate box must not produce garbage.
    const ControllerGeometry zero = layoutController({0, 0, 0, 0});
    check(zero.body.w <= 0 || zero.body.h <= 0,
          "an empty box yields an empty body");
}

void testSnapshot() {
    // readController(nullptr) stands in for "no pad", which is the usual
    // state on a build machine.
    const ControllerSnapshot none = readController(nullptr);
    check(!none.connected, "a null pad is not connected");
    check(!none.anyInput(), "a null pad reports no input");
    check(none.name.empty(), "a null pad has no name");

    ControllerSnapshot active;
    active.connected = true;
    check(!active.anyInput(), "a resting pad reports no input");

    active.buttons[SDL_CONTROLLER_BUTTON_A] = true;
    check(active.anyInput(), "a pressed button counts as input");

    active.buttons[SDL_CONTROLLER_BUTTON_A] = false;
    active.axes[SDL_CONTROLLER_AXIS_LEFTX] = 0.1f;
    check(!active.anyInput(), "a resting stick is below the dead zone");

    active.axes[SDL_CONTROLLER_AXIS_LEFTX] = 0.8f;
    check(active.anyInput(), "a deflected stick counts as input");

    // A stick pushed the other way must count too: comparing the raw value
    // rather than its magnitude would miss half of every axis.
    active.axes[SDL_CONTROLLER_AXIS_LEFTX] = -0.8f;
    check(active.anyInput(), "a negatively deflected stick counts as input");
}

void testEllipsize() {
    if (TTF_Init() != 0) {
        std::printf("skip  ellipsize: TTF_Init failed: %s\n", TTF_GetError());
        return;
    }

    TextCache text;
    std::string error;
    if (!text.open(1.0f, error)) {
        std::printf("skip  ellipsize: no usable font (%s)\n", error.c_str());
        TTF_Quit();
        return;
    }

    const std::string ascii = "The Legend of Zelda: A Link to the Past";
    const int full = text.width(FontRole::Body, ascii);
    check(full > 0, "a title measures wider than zero");

    checkEqual(text.ellipsize(FontRole::Body, ascii, full + 50), ascii,
               "a title that fits is returned unchanged");

    const std::string trimmed = text.ellipsize(FontRole::Body, ascii, full / 2);
    check(trimmed.size() < ascii.size(), "a long title is shortened");
    check(text.width(FontRole::Body, trimmed) <= full / 2,
          "the shortened title fits the budget");
    check(trimmed.size() >= 3, "the result keeps the ellipsis");
    check(!endsMidCodePoint(trimmed), "an ASCII result is valid UTF-8");

    check(text.ellipsize(FontRole::Body, ascii, 0).empty(),
          "a zero budget yields nothing");
    check(text.ellipsize(FontRole::Body, ascii, -10).empty(),
          "a negative budget yields nothing");

    const std::string multibyte =
        "\xC3\x89"              // E with acute
        "l\xC3\xA8ve "          // e with grave
        "\xE2\x98\x85 "         // star
        "Pok\xC3\xA9mon Rouge Feu \xE2\x80\x94 Version Fran\xC3\xA7" "aise";
    check(!endsMidCodePoint(multibyte), "the test string itself is valid");

    const int multibyte_full = text.width(FontRole::Body, multibyte);
    for (int budget = 4; budget < multibyte_full; budget += 7) {
        const std::string cut =
            text.ellipsize(FontRole::Body, multibyte, budget);
        if (endsMidCodePoint(cut)) {
            std::printf("FAIL  ellipsize split a code point at budget %d\n",
                        budget);
            ++failures;
            break;
        }
        if (text.width(FontRole::Body, cut) > budget) {
            std::printf("FAIL  ellipsize overran the budget %d\n", budget);
            ++failures;
            break;
        }
    }

    checkEqual(text.ellipsize(FontRole::Body, ascii, full / 2), trimmed,
               "ellipsize is deterministic");

    text.close();
    TTF_Quit();
}

} // namespace

void testHotkeys() {
    std::printf("hotkeys\n");
    emu::frontend::Hotkeys keys;

    // Every table entry must resolve to itself out of the box, or an action
    // is in the enum with no default and is silently unreachable.
    for (int i = 0; i < emu::frontend::kHotkeyCount; ++i) {
        const emu::frontend::HotkeySpec& spec = emu::frontend::kHotkeys[i];
        SDL_Keysym keysym{};
        keysym.scancode = spec.default_key;
        keysym.mod = 0;
        if (spec.default_mod & emu::frontend::kModShift) {
            keysym.mod |= KMOD_LSHIFT;
        }
        if (spec.default_mod & emu::frontend::kModCtrl) {
            keysym.mod |= KMOD_LCTRL;
        }
        if (spec.default_mod & emu::frontend::kModAlt) {
            keysym.mod |= KMOD_LALT;
        }
        check(keys.resolve(keysym) == spec.action,
              std::string("default resolves: ") + spec.key);
    }

    // No two actions may share a combination in the defaults.
    for (int i = 0; i < emu::frontend::kHotkeyCount; ++i) {
        const emu::frontend::HotkeyAction action =
            emu::frontend::kHotkeys[i].action;
        const emu::frontend::HotkeyBinding& binding = keys.binding(action);
        check(keys.conflict(binding.key, binding.mod, action) ==
                  emu::frontend::HotkeyAction::None,
              std::string("no default clash: ") +
                  emu::frontend::kHotkeys[i].key);
    }

    // A modifierless binding must not fire when a modifier is held, or
    // Shift+F10 would take a screenshot as well as open the gallery.
    SDL_Keysym shifted{};
    shifted.scancode = SDL_SCANCODE_F10;
    shifted.mod = KMOD_RSHIFT;
    check(keys.resolve(shifted) == emu::frontend::HotkeyAction::Gallery,
          "Shift+F10 is the gallery, not a screenshot");

    SDL_Keysym plain{};
    plain.scancode = SDL_SCANCODE_F10;
    plain.mod = 0;
    check(keys.resolve(plain) == emu::frontend::HotkeyAction::Screenshot,
          "plain F10 is still a screenshot");

    // Right-hand modifiers and the lock keys must not change the match.
    SDL_Keysym noisy{};
    noisy.scancode = SDL_SCANCODE_R;
    noisy.mod = KMOD_RCTRL | KMOD_NUM | KMOD_CAPS;
    check(keys.resolve(noisy) == emu::frontend::HotkeyAction::Reset,
          "Num Lock does not break Ctrl+R");

    // An unbound action must not match an unbound key press.
    keys.unbind(emu::frontend::HotkeyAction::DebugInfo);
    SDL_Keysym unknown{};
    unknown.scancode = SDL_SCANCODE_UNKNOWN;
    unknown.mod = 0;
    check(keys.resolve(unknown) == emu::frontend::HotkeyAction::None,
          "an unknown scancode resolves to nothing");

    // A clash is reported rather than silently stealing the combination.
    check(keys.conflict(SDL_SCANCODE_F1, emu::frontend::kModNone,
                        emu::frontend::HotkeyAction::Screenshot) ==
              emu::frontend::HotkeyAction::PauseMenu,
          "F1 is reported as taken by the pause menu");

    // Round-trip through the file.
    keys.bind(emu::frontend::HotkeyAction::Screenshot, SDL_SCANCODE_F7,
              emu::frontend::kModCtrl | emu::frontend::kModShift);
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "toaster-hotkeys-test.ini";
    std::string error;
    check(keys.save(file, error), "hotkeys save");

    emu::frontend::Hotkeys reloaded;
    check(reloaded.load(file, error), "hotkeys load");
    const emu::frontend::HotkeyBinding& round =
        reloaded.binding(emu::frontend::HotkeyAction::Screenshot);
    check(round.key == SDL_SCANCODE_F7, "scancode survived the round trip");
    check(round.mod == (emu::frontend::kModCtrl | emu::frontend::kModShift),
          "modifiers survived the round trip");
    check(!reloaded.binding(emu::frontend::HotkeyAction::DebugInfo).bound(),
          "an unbound action stays unbound across a round trip");

    checkEqual(emu::frontend::describeHotkey(round), "Ctrl+Shift+F7",
               "describeHotkey");

    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

void testRecentView() {
    std::printf("recent view\n");
    std::vector<FakeGame> games(3);
    games[0].title = "Alpha";
    games[0].path = "alpha.nes";
    games[0].last_played = 0;
    games[1].title = "Bravo";
    games[1].path = "bravo.nes";
    games[1].last_played = 1000;
    games[2].title = "Charlie";
    games[2].path = "charlie.nes";
    games[2].last_played = 2000;

    emu::frontend::ui::GameViewT<FakeGame> view;
    view.setSource(&games);
    view.setFilter(emu::frontend::ui::GameViewT<FakeGame>::Filter::Recent);
    view.refresh();

    // Recent was in the enum and ignored by refresh(), so it fell through to
    // All and showed everything, never-played games included.
    checkEqual(static_cast<int>(view.size()), 2,
               "never-played games are filtered out");
    if (view.size() == 2) {
        checkEqual(view.at(0).title, "Charlie", "newest first");
        checkEqual(view.at(1).title, "Bravo", "then the next newest");
    }

    // A zero timestamp must not sort to the front under LastPlayed.
    view.setFilter(emu::frontend::ui::GameViewT<FakeGame>::Filter::All);
    view.setSort(emu::frontend::ui::GameViewT<FakeGame>::Sort::LastPlayed);
    view.refresh();
    checkEqual(static_cast<int>(view.size()), 3, "All still shows everything");
    if (view.size() == 3) {
        checkEqual(view.at(2).title, "Alpha", "never-played sorts last");
    }
}

void testOverlayGeometry() {
    std::printf("overlay geometry\n");
    using emu::frontend::ui::Metrics;
    using emu::frontend::ui::overlayBodyRect;
    using emu::frontend::ui::overlayPanelRect;
    using emu::frontend::ui::overlaySlotRect;
    using emu::frontend::ui::overlayTabsRect;

    const auto contains = [](const SDL_Rect& outer, const SDL_Rect& inner) {
        return inner.x >= outer.x && inner.y >= outer.y &&
               inner.x + inner.w <= outer.x + outer.w &&
               inner.y + inner.h <= outer.y + outer.h;
    };
    const auto overlaps = [](const SDL_Rect& a, const SDL_Rect& b) {
        return a.x < b.x + b.w && b.x < a.x + a.w &&
               a.y < b.y + b.h && b.y < a.y + a.h;
    };

    // Three window sizes and two scales: the slot grid mixes a width-derived
    // cell width with a height-derived text block, which is the same unit
    // confusion that put the controller diagram's triggers outside its rect.
    const SDL_Rect windows[] = {
        {0, 0, 1280, 720}, {0, 0, 960, 540}, {0, 0, 2560, 1440}};
    const float scales[] = {1.0f, 2.0f};

    for (const SDL_Rect& window : windows) {
        for (const float scale : scales) {
            Metrics m;
            m.scale = scale;

            const SDL_Rect panel = overlayPanelRect(window, m);
            const SDL_Rect tabs = overlayTabsRect(panel, m);
            const SDL_Rect body = overlayBodyRect(panel, m);

            check(contains(window, panel), "the panel is inside the window");
            check(contains(panel, tabs), "the tab strip is inside the panel");
            check(contains(panel, body), "the body is inside the panel");
            check(!overlaps(tabs, body), "the tabs and the body do not overlap");
            check(body.w > 0 && body.h > 0, "the body has positive area");

            SDL_Rect cells[emu::frontend::SaveStateManager::kSlotCount];
            for (int slot = 0;
                 slot < emu::frontend::SaveStateManager::kSlotCount; ++slot) {
                cells[slot] = overlaySlotRect(body, m, slot);
                check(cells[slot].w > 0 && cells[slot].h > 0,
                      "a slot cell has positive area");
                check(cells[slot].x >= body.x &&
                          cells[slot].x + cells[slot].w <= body.x + body.w,
                      "a slot cell stays within the body horizontally");
            }

            // No two cells may touch: the grid is drawn without clipping
            // between cells, so an overlap would paint one thumbnail over
            // another and is invisible until it is on screen.
            for (int a = 0; a < emu::frontend::SaveStateManager::kSlotCount;
                 ++a) {
                for (int b = a + 1;
                     b < emu::frontend::SaveStateManager::kSlotCount; ++b) {
                    check(!overlaps(cells[a], cells[b]),
                          "slot cells " + std::to_string(a) + " and " +
                              std::to_string(b) + " do not overlap");
                }
            }

            // Slot 5 starts the second row, so it must sit below slot 0 and
            // share its column.
            checkEqual(cells[5].x, cells[0].x,
                       "slot 5 is under slot 0");
            check(cells[5].y > cells[0].y, "the second row is below the first");
        }
    }
}

int main() {
    testFuzzyMatch();
    testFoldCase();
    testSystemFromPath();
    testGameView();
    testLayout();
    testToRenderCoords();
    testEllipsize();
    testColorParsing();
    testThemePresets();
    testContrastAndRepair();
    testThemeFile();
    testControllerLayout();
    testSnapshot();
    testHotkeys();
    testRecentView();
    testOverlayGeometry();


    if (failures == 0) {
        std::printf("ui: all checks passed\n");
        return 0;
    }
    std::printf("ui: %d check%s failed\n", failures,
                failures == 1 ? "" : "s");
    return 1;
}
