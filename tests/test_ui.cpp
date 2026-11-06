// Tests for the parts of the UI layer that are pure logic: fuzzy search,
// system detection, the layout arithmetic and text ellipsizing. None of these
// need a window.
//
// A failure counter rather than assert(): the default build type is
// RelWithDebInfo, which defines NDEBUG and compiles assertions out. That is
// exactly what left every check in test_settings.cpp silently doing nothing.

#define SDL_MAIN_HANDLED
#include "ui/game_view.h"
#include "ui/layout.h"
#include "ui/text_cache.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdio>
#include <filesystem>
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

// --- fuzzy search ------------------------------------------------------------

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

    // A title that really starts with the query should outrank one where the
    // same letters only happen to appear, or search results are useless.
    int leading = 0;
    int scattered = 0;
    check(fuzzyMatch("metroid", "met", &leading), "leading match found");
    check(fuzzyMatch("the maze of eternity", "met", &scattered),
          "scattered match found");
    check(leading > scattered,
          "a leading match outranks a scattered one");

    // Ties break toward the shorter title.
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

// --- system detection --------------------------------------------------------

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

    // .iso is genuinely ambiguous between PSP and PS2. It resolves to PSP
    // because that is what the launcher assumes without --ps2.
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

// --- GameViewT --------------------------------------------------------------

struct FakeGame {
    std::filesystem::path path;
    std::string title;
    std::string year = "Unknown";
    bool favorite = false;
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

    // Default sort is by title.
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

    // Mutating the source in place needs an explicit invalidate, because
    // nothing else can see that it happened.
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

    // An empty source must not fall over.
    std::vector<FakeGame> empty;
    GameViewT<FakeGame> empty_view;
    empty_view.setSource(&empty);
    empty_view.refresh();
    check(empty_view.empty(), "an empty library yields an empty view");
    checkEqual(static_cast<int>(empty_view.indexOfPath("anything")), -1,
               "indexOfPath on an empty view reports -1");
}

// --- layout -----------------------------------------------------------------

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

    // A window far smaller than the design size must still produce a sane
    // layout. The old menu hard-coded 1000x650 and broke at anything else.
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

    // A taller window shows more rows. The old menu was fixed at twelve.
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

    // At 200% everything doubles, so a 1180px window is as cramped as a 590px
    // one and the detail pane goes away.
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

    // Grid mode.
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

    // rowRect and cellRect must agree with the rows region and step evenly.
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

    // A zero-column layout, which List mode produces, must not divide by zero.
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

    // Null arguments are allowed, for callers that only have one axis.
    x = 10;
    toRenderCoords(2.0f, &x, nullptr);
    checkEqual(x, 20, "a null y is ignored");
    toRenderCoords(2.0f, nullptr, nullptr);
}

// --- ellipsize --------------------------------------------------------------

bool endsMidCodePoint(const std::string& text) {
    // A UTF-8 string must not end on a continuation byte, and a lead byte
    // must be followed by the right number of them.
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

void testEllipsize() {
    // Needs a real font, which is not guaranteed on a build machine with no
    // system fonts. Skipping is better than failing for the wrong reason.
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

    // The real risk: trimming a multi-byte title one byte at a time would
    // leave a dangling continuation byte and draw a replacement glyph.
    const std::string multibyte =
        "\xC3\x89"              // E with acute
        "l\xC3\xA8ve "          // e with grave
        "\xE2\x98\x85 "         // star
        // Split before "aise" on purpose: a hex escape consumes every hex
        // digit that follows it, so "\xA7aise" would be read as the single
        // out-of-range value 0xA7A rather than 0xA7 then 'a'.
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

    // Caching must not change what is drawn: the same request twice is the
    // same answer, and the cache should not have grown a second entry.
    checkEqual(text.ellipsize(FontRole::Body, ascii, full / 2), trimmed,
               "ellipsize is deterministic");

    text.close();
    TTF_Quit();
}

} // namespace

int main() {
    testFuzzyMatch();
    testFoldCase();
    testSystemFromPath();
    testGameView();
    testLayout();
    testToRenderCoords();
    testEllipsize();

    if (failures == 0) {
        std::printf("ui: all checks passed\n");
        return 0;
    }
    std::printf("ui: %d check%s failed\n", failures,
                failures == 1 ? "" : "s");
    return 1;
}
