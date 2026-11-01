#include "memory.h"

#include <algorithm>

namespace emu::gb {

Memory::Memory() {
    reset();
}

void Memory::connect(Cartridge* cartridge) {
    cartridge_ = cartridge;
}

void Memory::reset() {
    vram_.fill(0);
    wram_.fill(0);
    hram_.fill(0);
    oam_.fill(0);

    joypad_select_ = 0x30;
    buttons_ = 0xFF;

    div_ = 0;
    tima_ = 0;
    tma_ = 0;
    tac_ = 0;
    lcd_control_ = 0x91;
    lcd_status_ = 0x85;
    scroll_y_ = 0;
    scroll_x_ = 0;
    current_line_ = 0;
    line_compare_ = 0;

    interrupt_flags_ = 0;
    interrupt_enable_ = 0;
}

uint8_t Memory::joypad() const {
    uint8_t result = 0xC0 | joypad_select_;

    if ((joypad_select_ & 0x10) == 0) {
        result |= buttons_ & 0x0F;
    } else {
        result |= (buttons_ >> 4) & 0x0F;
    }

    return result;
}

void Memory::setButtons(uint8_t buttons) {
    buttons_ = buttons;
}

uint8_t Memory::interruptFlags() const {
    return interrupt_flags_ | 0xE0;
}

void Memory::requestInterrupt(uint8_t mask) {
    interrupt_flags_ |= mask;
}

uint8_t Memory::interruptEnable() const {
    return interrupt_enable_;
}

void Memory::setInterruptEnable(uint8_t value) {
    interrupt_enable_ = value;
}

uint8_t Memory::read(uint16_t address) {
    if (address < 0x8000) {
        return cartridge_ ? cartridge_->read(address) : 0xFF;
    }

    if (address < 0xA000) {
        return vram_[address - 0x8000];
    }

    if (address < 0xC000) {
        return cartridge_ ? cartridge_->read(address) : 0xFF;
    }

    if (address < 0xE000) {
        return wram_[address - 0xC000];
    }

    if (address < 0xFE00) {
        return wram_[address - 0xE000];
    }

    if (address < 0xFEA0) {
        return oam_[address - 0xFE00];
    }

    if (address < 0xFF00) {
        return 0xFF;
    }

    if (address == 0xFF00) return joypad();
    if (address == 0xFF04) return div_;
    if (address == 0xFF05) return tima_;
    if (address == 0xFF06) return tma_;
    if (address == 0xFF07) return tac_;
    if (address == 0xFF40) return lcd_control_;
    if (address == 0xFF41) return lcd_status_;
    if (address == 0xFF42) return scroll_y_;
    if (address == 0xFF43) return scroll_x_;
    if (address == 0xFF44) return current_line_;
    if (address == 0xFF45) return line_compare_;
    if (address == 0xFF0F) return interruptFlags();

    if (address >= 0xFF80 && address < 0xFFFF) {
        return hram_[address - 0xFF80];
    }

    if (address == 0xFFFF) {
        return interrupt_enable_;
    }

    return 0xFF;
}

void Memory::write(uint16_t address, uint8_t value) {
    if (address < 0x8000) {
        if (cartridge_) cartridge_->write(address, value);
        return;
    }

    if (address < 0xA000) {
        vram_[address - 0x8000] = value;
        return;
    }

    if (address < 0xC000) {
        if (cartridge_) cartridge_->write(address, value);
        return;
    }

    if (address < 0xE000) {
        wram_[address - 0xC000] = value;
        return;
    }

    if (address < 0xFE00) {
        wram_[address - 0xE000] = value;
        return;
    }

    if (address < 0xFEA0) {
        oam_[address - 0xFE00] = value;
        return;
    }

    if (address < 0xFF00) {
        return;
    }

    if (address == 0xFF00) {
        joypad_select_ = value & 0x30;
        return;
    }

    if (address == 0xFF04) {
        div_ = 0;
        return;
    }

    if (address == 0xFF05) {
        tima_ = value;
        return;
    }

    if (address == 0xFF06) {
        tma_ = value;
        return;
    }

    if (address == 0xFF07) {
        tac_ = value & 0x07;
        return;
    }

    if (address == 0xFF40) {
        lcd_control_ = value;
        return;
    }

    if (address == 0xFF41) {
        lcd_status_ = static_cast<uint8_t>((lcd_status_ & 0x07) |
                                            (value & 0x78));
        return;
    }

    if (address == 0xFF42) {
        scroll_y_ = value;
        return;
    }

    if (address == 0xFF43) {
        scroll_x_ = value;
        return;
    }

    if (address == 0xFF44) {
        current_line_ = 0;
        return;
    }

    if (address == 0xFF45) {
        line_compare_ = value;
        return;
    }

    if (address == 0xFF0F) {
        interrupt_flags_ = value & 0x1F;
        return;
    }

    if (address >= 0xFF80 && address < 0xFFFF) {
        hram_[address - 0xFF80] = value;
        return;
    }

    if (address == 0xFFFF) {
        interrupt_enable_ = value;
    }
}

} // namespace emu::gb