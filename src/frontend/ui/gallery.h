#pragma once

#include "painter.h"
#include "text_cache.h"
#include "theme.h"
#include "input_map.h"

#include <SDL.h>

#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {
class ArtworkCache;
}

namespace emu::frontend::ui {

class GalleryScreen {
public:
    void open(const std::string& game_path);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }

    bool handleEvent(const SDL_Event& event, const SDL_Rect& window,
                     const Metrics& metrics, float display_scale);
    bool handleAction(UiAction action);

    void draw(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics,
              const SDL_Rect& window, emu::frontend::ArtworkCache& art);

    bool consumeDeleted(std::string& path);

private:
    SDL_Rect panelRect(const SDL_Rect& window, const Metrics& m) const;
    SDL_Rect bodyRect(const SDL_Rect& panel, const Metrics& m) const;
    SDL_Rect cellRectAt(const SDL_Rect& body, const Metrics& m,
                        int index) const;
    int columns(const SDL_Rect& body, const Metrics& m) const;

    void reload();
    void deleteSelected();

    bool open_ = false;
    bool full_screen_view_ = false;
    std::string game_path_;
    std::vector<std::filesystem::path> shots_;
    int selected_ = 0;
    int scroll_row_ = 0;
    int hot_ = -1;
    std::string deleted_;
    SDL_Rect last_body_{};
    SDL_Rect last_panel_{};
};

} // namespace emu::frontend::ui
