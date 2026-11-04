#pragma once

#include <string>
#include <vector>

namespace emu::frontend {

enum class PauseAction {
    Resume,
    SaveState,
    LoadState,
    ResetGame,
    ToggleVsync,
    ToggleFps,
    DecreaseVolume,
    IncreaseVolume,
    ToggleMute,
    ToggleFullscreen,
    Quit
};

struct PauseMenuItem {
    std::string label;
    PauseAction action;
};

class PauseMenu {
public:
    PauseMenu();

    void open();
    void close();
    void toggle();

    bool isOpen() const;
    void moveUp();
    void moveDown();

    PauseAction selectedAction() const;
    const std::string& selectedLabel() const;

    // Enough of the item list for a caller to draw the menu itself.
    std::size_t itemCount() const;
    std::size_t selectedIndex() const;
    const std::string& itemLabel(std::size_t index) const;

private:
    std::vector<PauseMenuItem> items_;
    std::size_t selected_ = 0;
    bool open_ = false;
};

} // namespace emu::frontend