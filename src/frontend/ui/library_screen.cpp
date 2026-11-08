#include "library_screen.h"

#include "../artwork_cache.h"
#include "controller_view.h"
#include "gallery.h"
#include "game_overlay.h"
#include "game_view.h"
#include "input_map.h"
#include "layout.h"
#include "painter.h"
#include "text_cache.h"
#include "theme.h"
#include "theme_io.h"
#include "theme_screen.h"
#include "widgets.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <random>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace emu::frontend::ui {
namespace {

std::uint64_t nowMs() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_GetTicks64();
#else
    return SDL_GetTicks();
#endif
}

enum : WidgetId {
    kIdSettings = 1,
    kIdPs1Bios,
    kIdPs2Bios,
    kIdCores,
    kIdLaunch,
    kIdOpen,
    kIdRescan,
    kIdToggleView,
    kIdTheme,
    kIdSearchField,
    kIdGallery,
    kIdOverlay,
    kIdRandom,

    kIdChipFirst = 100,
    kIdRowFirst = 1000
};

struct ChipSpec {
    const char* label;
    GameViewT<GameEntry>::Filter filter;
    SystemId system;
};

// Recent needs GameEntry::last_played, which adoptLibraryGames and the local
// recent.ini both fill now. The view forces a last-played sort for this chip:
// a title-ordered "recent" buries whatever was just played.
const ChipSpec kChips[] = {
    {"All", GameViewT<GameEntry>::Filter::All, SystemId::Unknown},
    {"Recent", GameViewT<GameEntry>::Filter::Recent, SystemId::Unknown},
    {"Favorites", GameViewT<GameEntry>::Filter::Favorites, SystemId::Unknown},
    {"NES", GameViewT<GameEntry>::Filter::System, SystemId::Nes},
    {"Game Boy", GameViewT<GameEntry>::Filter::System, SystemId::GameBoy},
    {"PS1", GameViewT<GameEntry>::Filter::System, SystemId::Ps1},
    {"PSP", GameViewT<GameEntry>::Filter::System, SystemId::Psp},
};
constexpr int kChipCount = static_cast<int>(sizeof(kChips) / sizeof(kChips[0]));

// "Today 14:02", "Yesterday", "3 days ago", "12 Mar 2025". An absolute
// timestamp is precise and unreadable; this is what a library wants.
std::string describeWhen(long long unix_seconds) {
    if (unix_seconds <= 0) return "Never";

    const std::time_t when = static_cast<std::time_t>(unix_seconds);
    const std::time_t now = std::time(nullptr);
    const double age = std::difftime(now, when);
    if (age < 0) return "Just now";

    const std::tm* local = std::localtime(&when);
    if (!local) return "Unknown";

    char buffer[64] = {};
    if (age < 60 * 60 * 12) {
        std::strftime(buffer, sizeof(buffer), "Today %H:%M", local);
        return buffer;
    }
    if (age < 60 * 60 * 36) return "Yesterday";
    if (age < 60.0 * 60 * 24 * 14) {
        const int days = static_cast<int>(age / (60 * 60 * 24));
        return std::to_string(days) + " days ago";
    }
    std::strftime(buffer, sizeof(buffer), "%d %b %Y", local);
    return buffer;
}

// A per-system tint, so the list reads at a glance.
SDL_Color systemTint(SystemId system, const Theme& theme) {
    switch (system) {
    case SystemId::Nes:     return SDL_Color{201, 103, 96, 255};
    case SystemId::GameBoy: return SDL_Color{141, 178, 116, 255};
    case SystemId::Chip8:   return SDL_Color{150, 143, 190, 255};
    case SystemId::Ps1:     return SDL_Color{126, 160, 201, 255};
    case SystemId::Psp:     return SDL_Color{120, 184, 190, 255};
    case SystemId::Ps2:     return SDL_Color{104, 128, 196, 255};
    case SystemId::Unknown:
    default:                return theme.border;
    }
}

struct State {
    std::size_t selected = 0;
    ViewMode mode = ViewMode::List;
    int chip = 0;
    std::string search;
    bool search_active = false;
};

struct Widgets {
    std::vector<Button> header;
    std::vector<Button> footer;
    std::vector<Chip> chips;
};

// Buttons are laid out right to left from the edge of their strip, so the row
// stays correct at any window width instead of being pinned to coordinates
// that only worked at 1000x650.
void appendRightToLeft(std::vector<Button>& into, const SDL_Rect& strip,
                       const Metrics& m, const TextCache& text,
                       std::initializer_list<std::pair<WidgetId, const char*>>
                           entries,
                       WidgetId primary) {
    int right = strip.x + strip.w;
    const int height = m.button_height();
    const int y = strip.y + (strip.h - height) / 2;

    for (const auto& entry : entries) {
        const int width =
            text.width(FontRole::Body, entry.second) + m.px(28);
        right -= width;

        Button button;
        button.id = entry.first;
        button.rect = {right, y, width, height};
        button.label = entry.second;
        button.primary = entry.first == primary;
        into.push_back(std::move(button));

        right -= m.px(8);
    }
}

// A uniform pick in [0, count). The generator is seeded once per process:
// reseeding on every press would repeat picks whenever two presses landed in
// the same clock tick.
std::size_t randomIndex(std::size_t count) {
    static std::mt19937 rng{std::random_device{}()};
    if (count <= 1) return 0;
    std::uniform_int_distribution<std::size_t> pick(0, count - 1);
    return pick(rng);
}

Widgets buildWidgets(const LibraryLayout& l, const Metrics& m,
                     const TextCache& text, const State& state,
                     bool have_selection) {
    Widgets widgets;

    widgets.header.reserve(6);
    // The old menu drew only the PS2 button but still handled clicks on an
    // undrawn PS1 rect beside it, so installing a PS1 BIOS meant clicking
    // blank space. Both are real buttons now.
    appendRightToLeft(widgets.header, l.header, m, text,
                      {{kIdOverlay, "Settings"},
                       {kIdTheme, "Theme"},
                       {kIdGallery, "Gallery"},
                       {kIdCores, "Install Cores"},
                       {kIdPs2Bios, "PS2 BIOS"},
                       {kIdPs1Bios, "PS1 BIOS"}},
                      kNoWidget);

    widgets.footer.reserve(5);
    appendRightToLeft(widgets.footer, l.footer, m, text,
                      {{kIdLaunch, "Launch"},
                       {kIdOpen, "Open Game"},
                       {kIdRandom, "Surprise Me"},
                       {kIdRescan, "Rescan"},
                       {kIdToggleView,
                        state.mode == ViewMode::List ? "Grid" : "List"}},
                      kIdLaunch);

    // Launch and the random pick both mean nothing with an empty library, and
    // a disabled button says so more clearly than one that does nothing.
    for (Button& button : widgets.footer) {
        if (button.id == kIdLaunch || button.id == kIdRandom) {
            button.enabled = have_selection;
        }
    }

    widgets.chips.reserve(kChipCount);
    int x = l.chips.x;
    for (int i = 0; i < kChipCount; ++i) {
        const int width =
            text.width(FontRole::Small, kChips[i].label) + m.px(22);
        // Chips that would spill past the pane are dropped rather than drawn
        // half off the edge.
        if (x + width > l.chips.x + l.chips.w) break;

        Chip chip;
        chip.id = kIdChipFirst + static_cast<WidgetId>(i);
        chip.rect = {x, l.chips.y, width, l.chips.h};
        chip.label = kChips[i].label;
        chip.selected = i == state.chip;
        widgets.chips.push_back(std::move(chip));

        x += width + m.px(6);
    }

    return widgets;
}

// --- section 14: one row, drawn correctly -----------------------------------

void drawGameRow(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                 const Theme& theme, const Metrics& m, const SDL_Rect& row,
                 const GameEntry& game, bool selected, bool hovered) {
    if (selected) {
        painter.roundedRect(row, m.radius_small(), theme.accent_soft);
        painter.rect({row.x, row.y + m.px(8), m.px(3), row.h - m.px(16)},
                     theme.accent);
    } else if (hovered) {
        painter.roundedRect(row, m.radius_small(), theme.surface_hover);
    }

    // Clip: a title longer than the row used to wrap and draw straight over
    // the row below, because nothing constrained it.
    const SDL_Rect previous_clip = painter.pushClip(row);

    const SystemId system = systemFromPath(game.path.string());
    const int badge_w = m.px(42);
    const SDL_Rect badge{row.x + row.w - badge_w - m.px(12),
                         row.y + (row.h - m.px(20)) / 2, badge_w, m.px(20)};
    painter.roundedRect(badge, m.px(5), systemTint(system, theme));
    text.drawCentered(renderer, FontRole::Small, systemShortName(system),
                      badge, theme.text_on_accent);

    const int line = text.lineHeight(FontRole::Body);
    int x = row.x + m.px(14);

    if (game.favorite) {
        // A separate glyph in its own colour, rather than "* " spliced into
        // the title where it shifted the text and could not be styled.
        x += text.draw(renderer, FontRole::Body, "\xE2\x98\x85", x,
                       row.y + (row.h - line) / 2, theme.gold) +
             m.px(8);
    }

    const int title_max = badge.x - m.px(12) - x;
    text.draw(renderer, FontRole::Body,
              text.ellipsize(FontRole::Body, game.title, title_max), x,
              row.y + (row.h - line) / 2,
              selected ? theme.text : mix(theme.text, theme.text_muted, 0.25f));

    painter.popClip(previous_clip);
}

// --- section 15: one grid cell ----------------------------------------------

void drawGameCell(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                  const Theme& theme, const Metrics& m, const SDL_Rect& cell,
                  const GameEntry& game, ArtworkCache& artwork,
                  int* decode_budget, bool selected, bool hovered) {
    const SDL_Rect art{cell.x, cell.y, cell.w, cell.h - m.px(34)};

    if (selected) {
        painter.roundedRect(inset(cell, -m.px(3)), m.radius(), theme.accent);
    } else if (hovered) {
        painter.roundedRect(inset(cell, -m.px(2)), m.radius(),
                            theme.surface_hover);
    }

    // ArtworkCache::get decodes synchronously, so scrolling into twenty
    // uncached covers at once would stall the frame. A few are loaded per
    // frame and the rest show a placeholder that fills in over the next
    // handful of frames.
    SDL_Texture* cover = nullptr;
    if (!game.artwork.empty() && *decode_budget > 0) {
        cover = artwork.get(game.artwork);
        --*decode_budget;
    }

    if (cover) {
        int w = 0;
        int h = 0;
        SDL_QueryTexture(cover, nullptr, nullptr, &w, &h);
        if (w > 0 && h > 0) {
            // Cover, not letterbox: a grid of mismatched letterboxes looks
            // ragged. The overflow is clipped instead.
            const float fill = std::max(static_cast<float>(art.w) / w,
                                        static_cast<float>(art.h) / h);
            SDL_Rect fit{0, 0, static_cast<int>(w * fill),
                         static_cast<int>(h * fill)};
            fit.x = art.x + (art.w - fit.w) / 2;
            fit.y = art.y + (art.h - fit.h) / 2;

            const SDL_Rect previous_clip = painter.pushClip(art);
            SDL_RenderCopy(renderer, cover, nullptr, &fit);
            painter.popClip(previous_clip);
        }
    } else {
        painter.roundedRect(art, m.radius_small(), theme.surface_active);
        text.drawCentered(renderer, FontRole::Small,
                          systemShortName(systemFromPath(game.path.string())),
                          art, theme.text_muted);
    }

    const SDL_Rect caption{cell.x, art.y + art.h + m.px(6), cell.w, m.px(22)};
    text.drawCentered(renderer, FontRole::Small,
                      text.ellipsize(FontRole::Small, game.title, cell.w),
                      caption, selected ? theme.text : theme.text_muted);

    if (game.favorite) {
        text.draw(renderer, FontRole::Small, "\xE2\x98\x85", art.x + m.px(6),
                  art.y + m.px(6), theme.gold);
    }
}

void drawDetailPane(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                    const Theme& theme, const Metrics& m,
                    const LibraryLayout& l, const GameEntry* game,
                    ArtworkCache& artwork) {
    drawPanel(renderer, painter, theme, m, l.detail_panel);

    if (!game) {
        text.drawCentered(renderer, FontRole::Body, "Nothing selected",
                          l.detail_panel, theme.text_muted);
        return;
    }

    if (SDL_Texture* cover = artwork.get(game->artwork)) {
        int w = 0;
        int h = 0;
        SDL_QueryTexture(cover, nullptr, nullptr, &w, &h);
        if (w > 0 && h > 0) {
            // Letterbox here rather than crop: the detail pane is the one
            // place the whole cover should be visible.
            const float scale = std::min(static_cast<float>(l.art.w) / w,
                                         static_cast<float>(l.art.h) / h);
            SDL_Rect fit{0, 0, static_cast<int>(w * scale),
                         static_cast<int>(h * scale)};
            fit.x = l.art.x + (l.art.w - fit.w) / 2;
            fit.y = l.art.y + (l.art.h - fit.h) / 2;
            SDL_RenderCopy(renderer, cover, nullptr, &fit);
        }
    } else {
        painter.roundedRect(l.art, m.radius_small(), theme.surface_active);
        text.drawCentered(renderer, FontRole::Small,
                          artwork.ready() ? "No cover" : "Cover art disabled",
                          l.art, theme.text_muted);
    }

    const SDL_Rect previous_clip = painter.pushClip(l.detail_body);

    int y = l.detail_body.y;
    const int x = l.detail_body.x;
    const int width = l.detail_body.w;

    text.draw(renderer, FontRole::Title,
              text.ellipsize(FontRole::Title, game->title, width), x, y,
              theme.gold);
    y += text.lineHeight(FontRole::Title) + m.px(4);

    const SystemId system = systemFromPath(game->path.string());
    const SDL_Rect badge{x, y, m.px(52), m.px(20)};
    painter.roundedRect(badge, m.px(5), systemTint(system, theme));
    text.drawCentered(renderer, FontRole::Small, systemShortName(system),
                      badge, theme.text_on_accent);
    if (game->favorite) {
        text.draw(renderer, FontRole::Small, "\xE2\x98\x85  Favorite",
                  x + badge.w + m.px(10), y + m.px(3), theme.gold);
    }
    y += badge.h + m.px(14);

    // A label column and a value column, rather than the space-padded
    // "Developer  " strings the old pane used, which only lined up in a
    // monospaced font it never had.
    const int label_w = m.px(86);
    const std::string played = describeWhen(game->last_played);
    const std::string plays = game->play_count > 0
        ? std::to_string(game->play_count) +
              (game->play_count == 1 ? " time" : " times")
        : std::string("Never");
    const std::pair<const char*, const std::string*> fields[] = {
        {"Developer", &game->developer},
        {"Year", &game->year},
        {"Genre", &game->genre},
        {"Last played", &played},
        {"Played", &plays},
    };

    for (const auto& field : fields) {
        text.draw(renderer, FontRole::Small, field.first, x,
                  y + m.px(2), theme.text_muted);
        text.draw(renderer, FontRole::Body,
                  text.ellipsize(FontRole::Body, *field.second,
                                 width - label_w),
                  x + label_w, y, theme.text);
        y += text.lineHeight(FontRole::Body) + m.px(6);
    }

    if (!game->tags.empty()) {
        text.draw(renderer, FontRole::Small, "Tags", x, y + m.px(2),
                  theme.text_muted);
        text.draw(renderer, FontRole::Body,
                  text.ellipsize(FontRole::Body, game->tags, width - label_w),
                  x + label_w, y, theme.text_muted);
        y += text.lineHeight(FontRole::Body) + m.px(6);
    }

    y += m.px(8);
    painter.hline(x, y, width, m.border(), theme.border);
    y += m.px(12);

    // The label plus at least two lines of body, or the section is not worth
    // the space: adding the last-played rows above pushed the description far
    // enough down that a one-line check left a heading and half a sentence
    // sliced off by the clip rect.
    const int remaining = l.detail_body.y + l.detail_body.h - y;
    const int needed = text.lineHeight(FontRole::Small) + m.px(6) +
                       text.lineHeight(FontRole::Body) * 2;
    if (remaining >= needed) {
        text.draw(renderer, FontRole::Small, "DESCRIPTION", x, y,
                  theme.text_muted);
        y += text.lineHeight(FontRole::Small) + m.px(6);
        // Wrapping is safe here: the clip rect stops it running past the
        // panel however long the description is.
        text.draw(renderer, FontRole::Body, game->description, x, y,
                  theme.text, width);
    }

    painter.popClip(previous_clip);
}

} // namespace

std::string runLibraryScreen(std::vector<GameEntry>& games,
                             const std::filesystem::path& folder,
                             const LibraryActions& actions) {
    // Initialise but never tear down: main.cpp holds its own font for the
    // settings windows, and TTF_Quit here would pull it out from under them.
    if (!TTF_WasInit() && TTF_Init() != 0) {
        std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
        return {};
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Window* window = SDL_CreateWindow(
        "Toaster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 740,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "Could not create the library window: %s\n",
                     SDL_GetError());
        return {};
    }
    SDL_SetWindowMinimumSize(window, 640, 480);

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::fprintf(stderr, "Could not create a renderer: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        return {};
    }

    float scale = displayScale(window, renderer);

    // The theme comes from settings.ini; "custom" reads theme.ini beside the
    // executable. A broken or missing theme file must never stop the library
    // opening, so every failure falls back to the default and says so.
    ThemeId theme_id = themeFromName(
        actions.themeName ? actions.themeName() : std::string("midnight"));
    Theme theme = themePreset(theme_id);

    if (theme_id == ThemeId::Custom) {
        std::vector<std::string> theme_warnings;
        std::string theme_error;
        if (!loadTheme("theme.ini", theme, theme_warnings, theme_error)) {
            std::fprintf(stderr, "theme.ini: %s\n", theme_error.c_str());
            theme = themePreset(ThemeId::Midnight);
            theme_id = ThemeId::Midnight;
        }
        for (const std::string& warning : theme_warnings) {
            std::fprintf(stderr, "theme.ini: %s\n", warning.c_str());
        }
    }

    // Whatever the source, a theme nobody could read is corrected before
    // anything is drawn with it. Without this a bad theme.ini would hide the
    // editor needed to fix it.
    theme = repairTheme(theme);
    Theme base_theme = theme;

    Metrics metrics;
    metrics.scale = scale;

    TextCache text;
    std::string font_error;
    if (!text.open(scale, font_error)) {
        std::fprintf(stderr, "%s\n", font_error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Toaster",
                                 font_error.c_str(), window);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return {};
    }

    // Outlives the block below, which owns everything tied to the renderer.
    std::string chosen;

    {
        Painter painter(renderer);
        ArtworkCache artwork(renderer);
        ToastStack toasts;
        Smoothed scroll;
        InputMap input;
        input.open();

        ThemeScreen theme_screen;
        GameOverlay settings_overlay;
        GalleryScreen gallery;

        // One watcher owns the pad handle and keeps it in step with
        // hot-plugging. InputMap opens its own handle for navigation; SDL
        // reference-counts those, so both closing is correct.
        ControllerWatcher pads;
        pads.open();
        bool show_controller = false;

        GameViewT<GameEntry> view;
        view.setSource(&games);

        // The library's overlay only shows the pages that mean anything with
        // no game running; the rest are reached from the pause menu. The
        // settings pointer being null is what disables the others, so an
        // install with no settings object still gets a usable screen.
        {
            OverlayHooks overlay_hooks;
            overlay_hooks.settings = actions.settings;
            overlay_hooks.hotkeys = actions.hotkeys;
            overlay_hooks.persistSettings = actions.persistSettings;
            overlay_hooks.pad = [&pads] { return pads.state(); };
            overlay_hooks.notify = [&toasts](std::string message,
                                             Toast::Kind kind) {
                toasts.push(std::move(message), kind, nowMs());
            };
            settings_overlay.setHooks(std::move(overlay_hooks));
        }

        State state;
        std::string result;
        bool running = true;
        bool dirty = true;

        WidgetId hot = kNoWidget;
        WidgetId pressed = kNoWidget;
        int mouse_x = -1;
        int mouse_y = -1;
        std::uint64_t previous_ms = nowMs();
        std::uint64_t last_click_ms = 0;
        long long last_click_row = -1;

        // --- helpers over the current frame's state -------------------------

        const auto outputSize = [&](int* w, int* h) {
            if (SDL_GetRendererOutputSize(renderer, w, h) != 0) {
                SDL_GetWindowSize(window, w, h);
            }
        };

        const auto currentLayout = [&] {
            int w = 0;
            int h = 0;
            outputSize(&w, &h);
            return layoutLibrary(w, h, metrics, state.mode);
        };

        // The pixel height of everything in the scroll region, measured from
        // the same functions that place the rows, so the two cannot drift.
        const auto contentHeight = [&](const LibraryLayout& l) {
            const std::size_t count = view.size();
            if (count == 0) return 0;
            if (state.mode == ViewMode::Grid) {
                const SDL_Rect last =
                    cellRect(l, static_cast<int>(count) - 1, 0);
                return last.y + last.h - l.rows.y;
            }
            const SDL_Rect last = rowRect(l, static_cast<int>(count) - 1, 0);
            return last.y + last.h - l.rows.y;
        };

        const auto maxScroll = [&](const LibraryLayout& l) {
            return std::max(0, contentHeight(l) - l.rows.h);
        };

        const auto clampScroll = [&](const LibraryLayout& l) {
            scroll.target = std::min(static_cast<float>(maxScroll(l)),
                                     std::max(0.0f, scroll.target));
        };

        // Keeps the selection on screen after a move, a filter change or a
        // resize.
        const auto scrollToSelection = [&](const LibraryLayout& l) {
            if (view.empty()) {
                scroll.target = 0.0f;
                return;
            }
            const SDL_Rect cell =
                state.mode == ViewMode::Grid
                    ? cellRect(l, static_cast<int>(state.selected), 0)
                    : rowRect(l, static_cast<int>(state.selected), 0);

            const int top = cell.y - l.rows.y;
            const int bottom = top + cell.h;

            if (top < static_cast<int>(scroll.target)) {
                scroll.target = static_cast<float>(top);
            } else if (bottom > static_cast<int>(scroll.target) + l.rows.h) {
                scroll.target = static_cast<float>(bottom - l.rows.h);
            }
            clampScroll(l);
        };

        const auto clampSelection = [&] {
            if (view.empty()) {
                state.selected = 0;
            } else if (state.selected >= view.size()) {
                state.selected = view.size() - 1;
            }
        };

        // Moving by a whole page, or a whole grid row, is the same operation
        // with a different step.
        const auto moveSelection = [&](int delta) {
            if (view.empty()) return;
            long long target = static_cast<long long>(state.selected) + delta;
            target = std::max(0LL, std::min(target,
                                            static_cast<long long>(
                                                view.size() - 1)));
            state.selected = static_cast<std::size_t>(target);
        };

        const auto applyChip = [&](int index) {
            if (index < 0 || index >= kChipCount) return;
            state.chip = index;
            view.setFilter(kChips[index].filter);
            view.setSystem(kChips[index].system);
        };

        const auto selectedGame = [&]() -> const GameEntry* {
            if (view.empty()) return nullptr;
            clampSelection();
            return &view.at(state.selected);
        };

        const auto launchSelected = [&] {
            const GameEntry* game = selectedGame();
            if (!game) return;
            result = game->path.string();
            if (actions.markPlayed) actions.markPlayed(result);
            running = false;
        };

        const auto toggleFavorite = [&] {
            const GameEntry* game = selectedGame();
            if (!game) return;
            if (actions.isScanning && actions.isScanning()) {
                toasts.push("Library is scanning; favorite not saved",
                            Toast::Kind::Error, nowMs());
                return;
            }
            const std::string path = game->path.string();
            const bool wanted = !game->favorite;

            if (actions.setFavorite && !actions.setFavorite(path, wanted)) {
                toasts.push("Could not save favorites.txt", Toast::Kind::Error,
                            nowMs());
                return;
            }
            // The source list changed in place, so the view has to rebuild;
            // the selection is restored by path because a Favorites filter
            // may have just dropped this row.
            view.invalidate();
            view.refresh();
            const long long index = view.indexOfPath(path);
            if (index >= 0) state.selected = static_cast<std::size_t>(index);
            clampSelection();

            toasts.push(wanted ? "Added to favorites"
                               : "Removed from favorites",
                        Toast::Kind::Success, nowMs());
        };

        // Everything clickable, in front-to-back order.
        const auto hitTestAll = [&](const LibraryLayout& l,
                                    const Widgets& widgets, int x,
                                    int y) -> WidgetId {
            if (WidgetId id = hitTest(widgets.header, x, y)) return id;
            if (WidgetId id = hitTest(widgets.footer, x, y)) return id;
            if (WidgetId id = hitTest(widgets.chips, x, y)) return id;
            if (pointInside(l.search, x, y)) return kIdSearchField;

            if (!pointInside(l.rows, x, y)) return kNoWidget;

            const int offset = static_cast<int>(scroll.current);
            for (std::size_t i = 0; i < view.size(); ++i) {
                const SDL_Rect cell =
                    state.mode == ViewMode::Grid
                        ? cellRect(l, static_cast<int>(i), offset)
                        : rowRect(l, static_cast<int>(i), offset);
                if (cell.y + cell.h < l.rows.y) continue;
                if (cell.y > l.rows.y + l.rows.h) break;
                if (pointInside(cell, x, y)) {
                    return kIdRowFirst + static_cast<WidgetId>(i);
                }
            }
            return kNoWidget;
        };

        const auto beginSearch = [&] {
            state.search_active = true;
            SDL_StartTextInput();
            dirty = true;
        };

        const auto endSearch = [&](bool clear) {
            state.search_active = false;
            SDL_StopTextInput();
            if (clear) {
                state.search.clear();
                view.setQuery(std::string());
                state.selected = 0;
            }
            dirty = true;
        };

        // A widget press, from the mouse or from Enter on the focused button.
        const auto activate = [&](WidgetId id) {
            switch (id) {
            case kIdOverlay:
                settings_overlay.open(OverlayPage::Video);
                dirty = true;
                break;
            case kIdGallery:
                gallery.open(std::string());
                dirty = true;
                break;
            case kIdPs1Bios:
                if (actions.installPs1Bios) actions.installPs1Bios();
                dirty = true;
                break;
            case kIdPs2Bios:
                if (actions.installPs2Bios) actions.installPs2Bios();
                dirty = true;
                break;
            case kIdCores:
                if (actions.installCores) actions.installCores();
                dirty = true;
                break;
            case kIdLaunch:
                launchSelected();
                break;
            case kIdRandom:
                if (!view.empty()) {
                    // Picked from the filtered view, not the whole library,
                    // so the search box and the chips still mean something.
                    // The redraw below scrolls the pick into sight.
                    state.selected = randomIndex(view.size());
                    dirty = true;
                }
                break;
            case kIdOpen:
                if (actions.browseForGame) {
                    const std::string chosen = actions.browseForGame();
                    if (!chosen.empty()) {
                        result = chosen;
                        running = false;
                    }
                }
                dirty = true;
                break;
            case kIdRescan:
                if (actions.rescan) actions.rescan();
                toasts.push("Rescanning the library", Toast::Kind::Info,
                            nowMs());
                dirty = true;
                break;
            case kIdToggleView:
                state.mode = state.mode == ViewMode::List ? ViewMode::Grid
                                                          : ViewMode::List;
                dirty = true;
                break;
            case kIdTheme:
                theme_screen.open(theme_id, base_theme);
                dirty = true;
                break;
            case kIdSearchField:
                beginSearch();
                break;
            default:
                if (id >= kIdChipFirst && id < kIdChipFirst + kChipCount) {
                    applyChip(static_cast<int>(id - kIdChipFirst));
                    dirty = true;
                }
                break;
            }
        };

        const auto handleAction = [&](UiAction action,
                                      const LibraryLayout& l) {
            const int page = std::max(1, l.visible_rows);
            const int row_step =
                state.mode == ViewMode::Grid ? std::max(1, l.columns) : 1;

            switch (action) {
            case UiAction::Up:       moveSelection(-row_step); break;
            case UiAction::Down:     moveSelection(row_step); break;
            case UiAction::Left:
                if (state.mode == ViewMode::Grid) moveSelection(-1);
                break;
            case UiAction::Right:
                if (state.mode == ViewMode::Grid) moveSelection(1);
                break;
            case UiAction::PageUp:   moveSelection(-page * row_step); break;
            case UiAction::PageDown: moveSelection(page * row_step); break;
            case UiAction::Home:     state.selected = 0; break;
            case UiAction::End:
                if (!view.empty()) state.selected = view.size() - 1;
                break;
            case UiAction::Confirm:  launchSelected(); break;
            case UiAction::Cancel:   running = false; break;
            case UiAction::ToggleFavorite: toggleFavorite(); break;
            case UiAction::ToggleView:     activate(kIdToggleView); break;
            case UiAction::Search:         beginSearch(); break;
            case UiAction::Refresh:        activate(kIdRescan); break;
            case UiAction::NextTab:
                applyChip((state.chip + 1) % kChipCount);
                break;
            case UiAction::PreviousTab:
                applyChip((state.chip + kChipCount - 1) % kChipCount);
                break;
            case UiAction::Menu:     activate(kIdOverlay); break;
            case UiAction::None:
            default:
                return;
            }
            dirty = true;
        };

        // --- the loop -------------------------------------------------------

        while (running) {
            const std::uint64_t now_ms = nowMs();
            const std::uint32_t dt_ms =
                static_cast<std::uint32_t>(now_ms - previous_ms);
            const float dt = dt_ms / 1000.0f;
            previous_ms = now_ms;

            if (actions.collectScan && actions.collectScan()) {
                view.invalidate();
                dirty = true;
            }
            if (view.refresh()) {
                clampSelection();
                dirty = true;
            }

            LibraryLayout layout = currentLayout();

            // Animation is the only reason to run without an event, so the
            // loop blocks when nothing is moving. Idle costs nothing and
            // scrolling stays smooth.
            const bool scrolling = scroll.step(dt);
            // The controller diagram and the theme editor both animate, so
            // the loop has to keep running while either is up.
            const bool animating = scrolling || state.search_active ||
                                   toasts.active(now_ms) ||
                                   theme_screen.isOpen() ||
                                   settings_overlay.isOpen() ||
                                   gallery.isOpen() ||
                                   (show_controller &&
                                    pads.state().connected) ||
                                   (actions.isScanning && actions.isScanning());
            if (scrolling) dirty = true;

            if (const UiAction repeated = input.repeat(dt_ms);
                repeated != UiAction::None) {
                handleAction(repeated, layout);
            }

            SDL_Event event;
            const bool got_event = animating
                                       ? SDL_PollEvent(&event) != 0
                                       : SDL_WaitEventTimeout(&event, 400) != 0;

            if (got_event) {
                // Built once per drain rather than per event: the layout
                // cannot change between events in one queue.
                const Widgets widgets =
                    buildWidgets(layout, metrics, text, state, !view.empty());

                do {
                    // Hot-plugging is noticed whatever is on screen, so the
                    // diagram and the toasts are never stale.
                    if (pads.handleEvent(event)) {
                        if (pads.justConnected()) {
                            toasts.push(pads.state().name + " connected",
                                        Toast::Kind::Success, now_ms);
                            // Showing the diagram answers "did it work?"
                            // without the user going to look for a screen.
                            if (!theme_screen.isOpen()) show_controller = true;
                        }
                        if (pads.justDisconnected()) {
                            toasts.push("Controller disconnected",
                                        Toast::Kind::Error, now_ms);
                            show_controller = false;
                        }
                        pads.acknowledge();
                        dirty = true;
                    }

                    // An open overlay owns the keyboard and the mouse. The
                    // library behind it must not also act on the event, or a
                    // click meant for the dialog launches a game.
                    // The overlays are checked before the theme editor and
                    // before the library, so a click meant for one of them
                    // cannot also launch a game behind it.
                    if (settings_overlay.isOpen()) {
                        if (settings_overlay.handleEvent(
                                event, layout.window, metrics, scale)) {
                            dirty = true;
                        } else if ((event.type == SDL_KEYDOWN ||
                                    event.type == SDL_CONTROLLERBUTTONDOWN) &&
                                   settings_overlay.handleAction(
                                       input.translate(&event))) {
                            dirty = true;
                        }
                        if (event.type == SDL_QUIT) running = false;
                        continue;
                    }

                    if (gallery.isOpen()) {
                        if (gallery.handleEvent(event, layout.window, metrics,
                                                scale)) {
                            dirty = true;
                        } else if ((event.type == SDL_KEYDOWN ||
                                    event.type == SDL_CONTROLLERBUTTONDOWN) &&
                                   gallery.handleAction(
                                       input.translate(&event))) {
                            dirty = true;
                        }
                        std::string deleted;
                        if (gallery.consumeDeleted(deleted)) {
                            toasts.push("Deleted " + deleted,
                                        Toast::Kind::Info, now_ms);
                        }
                        if (event.type == SDL_QUIT) running = false;
                        continue;
                    }

                    if (theme_screen.isOpen()) {
                        if (theme_screen.handleEvent(event, layout.window,
                                                     metrics, scale)) {
                            dirty = true;
                        }
                        if (event.type == SDL_KEYDOWN ||
                            event.type == SDL_CONTROLLERBUTTONDOWN) {
                            if (theme_screen.handleAction(
                                    input.translate(&event))) {
                                dirty = true;
                            }
                        }

                        if (theme_screen.wantsSave()) {
                            std::string theme_error;
                            if (saveTheme("theme.ini", theme_screen.preview(),
                                          theme_error)) {
                                base_theme =
                                    repairTheme(theme_screen.preview());
                                if (actions.setThemeName) {
                                    actions.setThemeName(themeName(
                                        theme_screen.selectedPreset()));
                                }
                                toasts.push("Theme saved to theme.ini",
                                            Toast::Kind::Success, now_ms);
                            } else {
                                toasts.push(theme_error, Toast::Kind::Error,
                                            now_ms);
                            }
                            theme_screen.acknowledge();
                            dirty = true;
                        }
                        if (theme_screen.wantsReset()) {
                            base_theme = themePreset(ThemeId::Midnight);
                            if (actions.setThemeName) {
                                actions.setThemeName("midnight");
                            }
                            theme_screen.acknowledge();
                            dirty = true;
                        }
                        continue;
                    }

                    if (show_controller) {
                        // Every pad button has to stay testable, so only the
                        // keyboard and a click dismiss the diagram.
                        const bool dismiss =
                            (event.type == SDL_KEYDOWN &&
                             !event.key.repeat &&
                             (event.key.keysym.sym == SDLK_F2 ||
                              event.key.keysym.sym == SDLK_ESCAPE)) ||
                            event.type == SDL_MOUSEBUTTONDOWN;
                        if (dismiss) show_controller = false;
                        if (event.type == SDL_QUIT) running = false;
                        dirty = true;
                        continue;
                    }

                    switch (event.type) {
                    case SDL_QUIT:
                        running = false;
                        break;

                    case SDL_MOUSEMOTION: {
                        mouse_x = event.motion.x;
                        mouse_y = event.motion.y;
                        // Mouse positions arrive in logical pixels while
                        // everything is drawn in output pixels. Skip this and
                        // clicks land offset on a HiDPI display.
                        toRenderCoords(scale, &mouse_x, &mouse_y);

                        // Repaint only when the hovered widget changed,
                        // instead of on every motion event.
                        const WidgetId under =
                            hitTestAll(layout, widgets, mouse_x, mouse_y);
                        if (under != hot) {
                            hot = under;
                            dirty = true;
                        }
                        break;
                    }

                    case SDL_MOUSEBUTTONDOWN: {
                        if (event.button.button != SDL_BUTTON_LEFT) break;
                        mouse_x = event.button.x;
                        mouse_y = event.button.y;
                        toRenderCoords(scale, &mouse_x, &mouse_y);

                        pressed = hitTestAll(layout, widgets, mouse_x, mouse_y);
                        hot = pressed;

                        if (pressed >= kIdRowFirst) {
                            const auto row =
                                static_cast<long long>(pressed - kIdRowFirst);
                            state.selected = static_cast<std::size_t>(row);

                            // A second click on the same row within the
                            // double-click window launches it.
                            const bool double_click =
                                row == last_click_row &&
                                now_ms - last_click_ms < 400;
                            last_click_row = row;
                            last_click_ms = now_ms;
                            if (double_click) launchSelected();
                        } else if (pressed == kNoWidget) {
                            if (state.search_active) endSearch(false);
                        }
                        dirty = true;
                        break;
                    }

                    case SDL_MOUSEBUTTONUP: {
                        if (event.button.button != SDL_BUTTON_LEFT) break;
                        mouse_x = event.button.x;
                        mouse_y = event.button.y;
                        toRenderCoords(scale, &mouse_x, &mouse_y);

                        const WidgetId released =
                            hitTestAll(layout, widgets, mouse_x, mouse_y);
                        // Only fire when press and release landed on the same
                        // widget, so dragging off a button cancels it.
                        if (released != kNoWidget && released == pressed &&
                            released < kIdRowFirst) {
                            activate(released);
                        }
                        pressed = kNoWidget;
                        dirty = true;
                        break;
                    }

                    case SDL_MOUSEWHEEL: {
                        const int step = metrics.row_height() * 3;
                        scroll.target -= static_cast<float>(event.wheel.y * step);
                        clampScroll(layout);
                        dirty = true;
                        break;
                    }

                    case SDL_TEXTINPUT:
                        if (state.search_active) {
                            state.search += event.text.text;
                            view.setQuery(state.search);
                            state.selected = 0;
                            scroll.target = 0.0f;
                            dirty = true;
                        }
                        break;

                    case SDL_KEYDOWN:
                        // While the search field has focus it takes the
                        // keyboard, so typing "f" filters rather than
                        // favouriting.
                        if (state.search_active) {
                            switch (event.key.keysym.sym) {
                            case SDLK_ESCAPE:
                                endSearch(true);
                                break;
                            case SDLK_RETURN:
                            case SDLK_KP_ENTER:
                                endSearch(false);
                                break;
                            case SDLK_BACKSPACE:
                                if (!state.search.empty()) {
                                    // Erase a whole code point, not a byte.
                                    do {
                                        state.search.pop_back();
                                    } while (!state.search.empty() &&
                                             (static_cast<unsigned char>(
                                                  state.search.back()) &
                                              0xC0) == 0x80);
                                    view.setQuery(state.search);
                                    state.selected = 0;
                                    dirty = true;
                                }
                                break;
                            case SDLK_UP:
                            case SDLK_DOWN:
                                handleAction(event.key.keysym.sym == SDLK_UP
                                                 ? UiAction::Up
                                                 : UiAction::Down,
                                             layout);
                                break;
                            default:
                                break;
                            }
                            break;
                        }
                        if (event.key.keysym.sym == SDLK_F2) {
                            show_controller = true;
                            dirty = true;
                            break;
                        }
                        handleAction(input.translate(&event), layout);
                        break;

                    case SDL_WINDOWEVENT:
                        if (event.window.event ==
                                SDL_WINDOWEVENT_SIZE_CHANGED ||
                            event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                            // A window dragged to a display with different
                            // scaling needs its fonts reopened, not just the
                            // layout recomputed.
                            const float updated =
                                displayScale(window, renderer);
                            if (std::fabs(updated - scale) > 0.01f) {
                                scale = updated;
                                metrics.scale = scale;
                                if (!text.rescale(scale, font_error)) {
                                    std::fprintf(stderr, "%s\n",
                                                 font_error.c_str());
                                    running = false;
                                }
                            }
                            layout = currentLayout();
                            clampScroll(layout);
                            dirty = true;
                        }
                        break;

                    default:
                        handleAction(input.translate(&event), layout);
                        break;
                    }
                } while (running && SDL_PollEvent(&event));
            }

            if (!running) break;

            // Edits are previewed by drawing the whole library in them, and
            // reverted by simply not doing so once the editor closes.
            theme = theme_screen.isOpen() ? theme_screen.preview()
                                          : base_theme;

            if (!dirty) continue;

            layout = currentLayout();
            clampSelection();
            scrollToSelection(layout);

            const Widgets widgets =
                buildWidgets(layout, metrics, text, state, !view.empty());
            const int offset = static_cast<int>(scroll.current);
            const bool scanning =
                actions.isScanning && actions.isScanning();

            // --- paint ------------------------------------------------------

            painter.rect(layout.window, theme.bg);

            text.draw(renderer, FontRole::Title, "TOASTER", layout.header.x,
                      layout.header.y + metrics.px(4), theme.accent);
            text.draw(renderer, FontRole::Small, "GAME LIBRARY",
                      layout.header.x,
                      layout.header.y + metrics.px(4) +
                          text.lineHeight(FontRole::Title),
                      theme.text_muted);

            for (const Button& button : widgets.header) {
                drawButton(renderer, painter, text, theme, metrics, button, hot,
                           pressed, false);
            }

            drawPanel(renderer, painter, theme, metrics, layout.list_panel);
            drawSearchField(renderer, painter, text, theme, metrics,
                            layout.search, state.search, state.search_active,
                            (now_ms / 500) % 2 == 0);

            for (const Chip& chip : widgets.chips) {
                drawChip(renderer, painter, text, theme, metrics, chip, hot);
            }

            if (view.empty()) {
                const int line = text.lineHeight(FontRole::Body);
                int y = layout.rows.y + metrics.px(8);
                text.draw(renderer, FontRole::BodyBold,
                          state.search.empty() ? "No games found yet"
                                               : "Nothing matches that search",
                          layout.rows.x + metrics.px(4), y, theme.gold);
                y += line + metrics.px(10);

                if (state.search.empty()) {
                    text.draw(renderer, FontRole::Body,
                              "Add supported games to this folder:",
                              layout.rows.x + metrics.px(4), y, theme.text);
                    y += line + metrics.px(6);
                    text.draw(renderer, FontRole::Small,
                              text.ellipsize(FontRole::Small, folder.string(),
                                             layout.rows.w - metrics.px(8)),
                              layout.rows.x + metrics.px(4), y,
                              theme.text_muted);
                    y += line + metrics.px(10);
                    text.draw(renderer, FontRole::Body,
                              "Then press Rescan.",
                              layout.rows.x + metrics.px(4), y,
                              theme.text_muted);
                }
            } else {
                // Clip, so a part-scrolled row cannot draw outside the pane.
                const SDL_Rect previous_clip = painter.pushClip(layout.rows);
                int budget = 4;   // covers decoded this frame, at most

                for (std::size_t i = 0; i < view.size(); ++i) {
                    const SDL_Rect cell =
                        state.mode == ViewMode::Grid
                            ? cellRect(layout, static_cast<int>(i), offset)
                            : rowRect(layout, static_cast<int>(i), offset);

                    if (cell.y + cell.h < layout.rows.y) continue;
                    if (cell.y > layout.rows.y + layout.rows.h) break;

                    const bool selected = i == state.selected;
                    const bool hovered =
                        hot == kIdRowFirst + static_cast<WidgetId>(i);

                    if (state.mode == ViewMode::Grid) {
                        drawGameCell(renderer, painter, text, theme, metrics,
                                     cell, view.at(i), artwork, &budget,
                                     selected, hovered);
                    } else {
                        drawGameRow(renderer, painter, text, theme, metrics,
                                    cell, view.at(i), selected, hovered);
                    }
                }
                painter.popClip(previous_clip);

                drawScrollbar(renderer, painter, theme, metrics,
                              layout.scrollbar, contentHeight(layout),
                              layout.rows.h, offset);
            }

            if (layout.two_pane) {
                drawDetailPane(renderer, painter, text, theme, metrics, layout,
                               selectedGame(), artwork);
            }

            for (const Button& button : widgets.footer) {
                drawButton(renderer, painter, text, theme, metrics, button, hot,
                           pressed, false);
            }

            // Footer status: the count always, the service totals only when a
            // scan is not holding the library's state.
            std::string status =
                std::to_string(view.size()) +
                (view.size() == 1 ? " game" : " games");
            if (scanning) {
                status += "   Scanning...";
            } else if (actions.statusLine) {
                const std::string extra = actions.statusLine();
                if (!extra.empty()) status += "   " + extra;
            }
            text.draw(renderer, FontRole::Small, status, layout.footer.x,
                      layout.footer.y + metrics.px(6), theme.text_muted);
            text.draw(renderer, FontRole::Small,
                      "Enter launches   F favorite   G grid   / search   "
                      "F2 controller   Esc quits",
                      layout.footer.x,
                      layout.footer.y + metrics.px(6) +
                          text.lineHeight(FontRole::Small) + metrics.px(2),
                      mix(theme.text_muted, theme.bg, 0.3f));

            if (show_controller) {
                painter.rect(layout.window, SDL_Color{0, 0, 0, 170});
                const SDL_Rect panel = inset(layout.window, metrics.px(80));
                drawPanel(renderer, painter, theme, metrics, panel);

                SDL_Rect diagram = inset(panel, metrics.pad());
                diagram.h -= metrics.px(26);
                drawController(renderer, painter, text, theme, metrics,
                               diagram, pads.state(),
                               SDL_CONTROLLER_BUTTON_INVALID, now_ms);

                text.drawCentered(
                    renderer, FontRole::Small,
                    "Press any control to test it.  F2 or Escape closes.",
                    {panel.x, panel.y + panel.h - metrics.px(28), panel.w,
                     metrics.px(20)},
                    theme.text_muted);
            }

            theme_screen.draw(renderer, painter, text, metrics, layout.window);
            settings_overlay.draw(renderer, painter, text, theme, metrics,
                                  layout.window, now_ms);
            gallery.draw(renderer, painter, text, theme, metrics,
                         layout.window, artwork);

            toasts.draw(renderer, painter, text, theme, metrics, layout.window,
                        now_ms);

            // Without this the whole frame goes to the back buffer and is
            // never shown, which is what left the window black in v0.2.0.
            SDL_RenderPresent(renderer);
            text.endFrame();
            dirty = false;
        }

        if (state.search_active) SDL_StopTextInput();
        pads.close();
        input.close();
        chosen = std::move(result);
    }

    // Painter, ArtworkCache and TextCache all hold textures created from this
    // renderer, so every one of them has to be gone before it is destroyed.
    // The first three go out of scope with the block above; this closes the
    // fourth.
    text.close();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return chosen;
}

} // namespace emu::frontend::ui
