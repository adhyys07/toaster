#include "pause_menu.h"

namespace emu::frontend {

PauseMenu::PauseMenu()
    : items_{
          {"Resume", PauseAction::Resume},
          {"Save State", PauseAction::SaveState},
          {"Load State", PauseAction::LoadState},
          {"Reset Game", PauseAction::ResetGame},
          {"Toggle VSync", PauseAction::ToggleVsync},
          {"Toggle FPS", PauseAction::ToggleFps},
          {"Decrease Volume", PauseAction::DecreaseVolume},
          {"Increase Volume", PauseAction::IncreaseVolume},
          {"Mute Audio", PauseAction::ToggleMute},
          {"Toggle Fullscreen", PauseAction::ToggleFullscreen},
          {"Quit", PauseAction::Quit},
      } {}

void PauseMenu::open() {
    open_ = true;
}

void PauseMenu::close() {
    open_ = false;
}

void PauseMenu::toggle() {
    open_ = !open_;
}

bool PauseMenu::isOpen() const {
    return open_;
}

void PauseMenu::moveUp() {
    if (items_.empty()) return;

    if (selected_ == 0)
        selected_ = items_.size() - 1;
    else
        --selected_;
}

void PauseMenu::moveDown() {
    if (items_.empty()) return;

    selected_ = (selected_ + 1) % items_.size();
}

PauseAction PauseMenu::selectedAction() const {
    return items_[selected_].action;
}

const std::string& PauseMenu::selectedLabel() const {
    return items_[selected_].label;
}

std::size_t PauseMenu::itemCount() const {
    return items_.size();
}

std::size_t PauseMenu::selectedIndex() const {
    return selected_;
}

const std::string& PauseMenu::itemLabel(std::size_t index) const {
    return items_[index < items_.size() ? index : selected_].label;
}

}