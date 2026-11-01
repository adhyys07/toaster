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

private:
    std::vector<PauseMenuItem> items_;
    std::size_t selected_ = 0;
    bool open_ = false;
};

} // namespace emu::frontend