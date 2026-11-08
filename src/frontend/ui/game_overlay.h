#pragma once

#include "../cheat_list.h"
#include "../hotkeys.h"
#include "../log_buffer.h"
#include "../save_file.h"
#include "../settings.h"
#include "controller_view.h"
#include "input_map.h"
#include "painter.h"
#include "text_cache.h"
#include "theme.h"
#include "widgets.h"

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace emu::frontend::ui {
struct SlotView
{
    bool exists = false;
    std::string when;
    std::string size;
    SDL_Texture* thumbnail = nullptr;
};

enum class OverlayPage {
    Pause,
    States,
    Video,
    Audio,
    Pad,
    Keys,
    Cheats,
    GameOptions,
    Saves,
    Log,
    About,
    Count
};

const char* overlayPageName(OverlayPage page);

struct OverlayHooks {
    emu::frontend::Settings* settings = nullptr;
    emu::frontend::GameSettings* game_settings = nullptr;
    emu::frontend::Hotkeys* hotkeys = nullptr;
    std::vector<emu::frontend::Cheat>* cheats = nullptr;

    std::function<void()> resume;
    std::function<void()> reset;
    std::function<void()> quit;
    std::function<void()> toggleFullscreen;
    std::function<void()> screenshot;
    std::function<void()> openGallery;

    std::function<void()> applySettings;
    std::function<void()> persistSettings;
    std::function<void()> persistGameOptions;
    std::function<void()> cheatsChanged;

    std::function<bool(int slot)> saveSlot;
    std::function<bool(int slot)> loadSlot;
    std::function<bool(int slot)> deleteSlot;
    std::function<SlotView(int slot)> slotView;

    std::function<ControllerSnapshot()> pad;
    std::function<void(std::string, Toast::Kind)> notify;

    // Battery-save backups. Empty function pointers mean the page shows
    // nothing rather than crashing: a core with no battery saves (CHIP-8,
    // WASM-4) simply has an empty list.
    std::function<std::vector<emu::frontend::SaveFile>()> saveFiles;
    std::function<bool(const emu::frontend::SaveFile&)> restoreSaveBackup;
    std::function<bool(const emu::frontend::SaveFile&)> deleteSaveFileHook;

    // Build metadata for the About page. All optional; a blank row is shown
    // for anything left null rather than the page being skipped.
    std::function<std::string()> buildInfo;
};

class GameOverlay {
public:
    void setHooks(OverlayHooks hooks);
    void open(OverlayPage page);
    void close();
    bool isOpen() const { return open_; }
    OverlayPage page() const { return page_; }

    bool handleEvent(const SDL_Event& event, const SDL_Rect& window,
                     const Metrics& metrics, float display_scale);

    bool handleAction(UiAction action);

    void draw(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics,
              const SDL_Rect& window, std::uint64_t now_ms);

    void invalidate() { dirty_ = true; }

    bool capturing() const { return capture_ >= 0; }

    bool captureButton(SDL_GameControllerButton button);

private:
    struct Row {
        std::string label;
        std::string value;
        int id = 0;
        bool header = false;
        bool enabled = true;
    };

    void rebuild();
    void move(int delta);
    // Up/Down and Left/Right mean different things on the slot grid than on a
    // row list, and both the keyboard and the pad have to agree about it.
    void moveVertical(int delta);
    // `from_pad` is not cosmetic: on the Controller page sideways edits a pad
    // binding, and letting the pad do that rebinds whatever the cursor is on
    // as soon as the stick leaves centre.
    void moveHorizontal(int delta, bool from_pad = false);
    void adjust(int delta);
    void activate();
    void switchPage(int delta);
    void cancelCapture();

    const Row* selectedRow() const;

    SDL_Rect panelRect(const SDL_Rect& window, const Metrics& m) const;
    SDL_Rect tabsRect(const SDL_Rect& panel, const Metrics& m) const;
    SDL_Rect bodyRect(const SDL_Rect& panel, const Metrics& m) const;
    SDL_Rect rowRectAt(const SDL_Rect& body, const Metrics& m, int index) const;
    SDL_Rect slotRectAt(const SDL_Rect& body, const Metrics& m, int slot) const;
    int visibleRows(const SDL_Rect& body, const Metrics& m) const;
    void clampScroll(const SDL_Rect& body, const Metrics& m);

    void drawRows(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                  const Theme& theme, const Metrics& m, const SDL_Rect& body);
    void drawStates(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                    const Theme& theme, const Metrics& m,
                    const SDL_Rect& body);
    void drawPadPage(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                     const Theme& theme, const Metrics& m,
                     const SDL_Rect& body, std::uint64_t now_ms);

    OverlayHooks hooks_;
    bool open_ = false;
    OverlayPage page_ = OverlayPage::Pause;
    std::vector<Row> rows_;
    int selected_ = 0;
    int scroll_ = 0;
    int slot_ = 1;
    bool dirty_ = true;
    int capture_ = -1;
    SDL_Rect last_body_{};
    SDL_Rect last_panel_{};
    int hot_row_ = -1;
    int hot_slot_ = -1;

    // Cached for the Saves page: rows only carry an id, and activate()
    // needs the actual SaveFile a Restore/Delete row refers to.
    std::vector<emu::frontend::SaveFile> save_files_;

    // 0 = Debug, shows everything; higher hides the quieter levels.
    int log_level_ = static_cast<int>(LogLevel::Info);
};

// Panel geometry, exposed so it can be tested headlessly. The slot grid mixes
// width-relative and height-relative units the same way the controller diagram
// did, and that class of mistake is invisible until it is on screen.
SDL_Rect overlayPanelRect(const SDL_Rect& window, const Metrics& m);
SDL_Rect overlayTabsRect(const SDL_Rect& panel, const Metrics& m);
SDL_Rect overlayBodyRect(const SDL_Rect& panel, const Metrics& m);
SDL_Rect overlaySlotRect(const SDL_Rect& body, const Metrics& m, int slot);

void drawRewindBar(SDL_Renderer* renderer, Painter& painter, TextCache& text,
                   const Theme& theme, const Metrics& metrics,
                   const SDL_Rect& window, std::size_t depth,
                   std::size_t capacity, float seconds);

}

