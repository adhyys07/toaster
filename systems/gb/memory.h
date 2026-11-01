#pragma once

#include "cartridge.h"

#include <array>
#include <cstdint>

namespace emu::gb {

class Memory {
public:
    Memory();

    void connect(Cartridge* cartridge);
    void reset();

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);

    uint8_t joypad() const;
    void setButtons(uint8_t buttons);

    uint8_t interruptFlags() const;
    void requestInterrupt(uint8_t mask);

    uint8_t interruptEnable() const;
    void setInterruptEnable(uint8_t value);

    uint8_t& timerDiv() { return div_; }
    uint8_t& timerTima() { return tima_; }
    uint8_t& timerTma() { return tma_; }
    uint8_t& timerTac() { return tac_; }

    uint8_t* vram() { return vram_.data(); }
    uint8_t* wram() { return wram_.data(); }
    uint8_t* oam() { return oam_.data(); }

private:
    Cartridge* cartridge_ = nullptr;

    std::array<uint8_t, 0x2000> vram_{};
    std::array<uint8_t, 0x2000> wram_{};
    std::array<uint8_t, 0x7F> hram_{};
    std::array<uint8_t, 0xA0> oam_{};

    uint8_t joypad_select_ = 0x30;
    uint8_t buttons_ = 0xFF;

    uint8_t div_ = 0;
    uint8_t tima_ = 0;
    uint8_t tma_ = 0;
    uint8_t tac_ = 0;
    uint8_t lcd_control_ = 0x91;
    uint8_t lcd_status_ = 0x85;
    uint8_t scroll_y_ = 0;
    uint8_t scroll_x_ = 0;
    uint8_t current_line_ = 0;
    uint8_t line_compare_ = 0;

    uint8_t interrupt_flags_ = 0;
    uint8_t interrupt_enable_ = 0;
};

} // namespace emu::gb