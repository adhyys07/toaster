#include "memory.h"

#include "apu.h"
#include "ppu.h"
#include "timer.h"

namespace emu::gb {

Memory::Memory() {
    reset();
}

void Memory::connect(Cartridge* cartridge) {
    cartridge_ = cartridge;
}

void Memory::connect(PPU* ppu, APU* apu, Timer* timer) {
    ppu_ = ppu;
    apu_ = apu;
    timer_ = timer;
}

void Memory::reset() {
    vram_.fill(0);
    wram_.fill(0);
    hram_.fill(0);
    oam_.fill(0);

    joypad_select_ = 0x30;
    buttons_ = 0xFF;

    interrupt_flags_ = 0;
    interrupt_enable_ = 0;

    serial_data_ = 0;
    serial_control_ = 0;
    serial_output_.clear();
}

uint8_t Memory::joypad() const {
    uint8_t result = static_cast<uint8_t>(0xC0 | joypad_select_);

    // A zero in the select bit picks that half, and a pressed button reads
    // as zero, which is why buttons_ is stored inverted by the caller.
    if ((joypad_select_ & 0x10) == 0) {
        result |= buttons_ & 0x0F;
    } else if ((joypad_select_ & 0x20) == 0) {
        result |= (buttons_ >> 4) & 0x0F;
    } else {
        result |= 0x0F;
    }

    return result;
}

void Memory::setButtons(uint8_t buttons) {
    // A bit going from 1 to 0 is a key being pressed, which raises the joypad
    // interrupt. Games use it to wake from STOP and from HALT.
    const uint8_t newly_pressed = static_cast<uint8_t>(buttons_ & ~buttons);
    buttons_ = buttons;
    if (newly_pressed != 0) requestInterrupt(0x10);
}

uint8_t Memory::interruptFlags() const {
    return static_cast<uint8_t>(interrupt_flags_ | 0xE0);
}

void Memory::requestInterrupt(uint8_t mask) {
    interrupt_flags_ |= mask & 0x1F;
}

void Memory::clearInterrupt(uint8_t mask) {
    interrupt_flags_ &= static_cast<uint8_t>(~mask);
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
        // Echo RAM mirrors C000-DDFF.
        return wram_[address - 0xE000];
    }

    if (address < 0xFEA0) {
        return oam_[address - 0xFE00];
    }

    if (address < 0xFF00) {
        return 0x00;
    }

    if (address == 0xFF00) return joypad();
    if (address == 0xFF01) return serial_data_;
    if (address == 0xFF02) return static_cast<uint8_t>(serial_control_ | 0x7E);
    if (address == 0xFF0F) return interruptFlags();

    if (address >= 0xFF04 && address <= 0xFF07) {
        return timer_ ? timer_->read(address) : 0xFF;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        return apu_ ? apu_->read(address) : 0xFF;
    }

    if (address >= 0xFF40 && address <= 0xFF4B) {
        return ppu_ ? ppu_->read(address) : 0xFF;
    }

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

    if (address == 0xFF01) {
        serial_data_ = value;
        return;
    }

    if (address == 0xFF02) {
        serial_control_ = value;
        // Starting a transfer with the internal clock publishes the byte and
        // completes immediately; nothing is attached to receive it.
        if ((value & 0x81) == 0x81) {
            if (serial_output_.size() < 4096) {
                serial_output_.push_back(static_cast<char>(serial_data_));
            }
            serial_control_ = static_cast<uint8_t>(value & 0x7F);
            requestInterrupt(0x08);
        }
        return;
    }

    if (address == 0xFF0F) {
        interrupt_flags_ = value & 0x1F;
        return;
    }

    if (address >= 0xFF04 && address <= 0xFF07) {
        if (timer_) timer_->write(address, value);
        return;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        if (apu_) apu_->write(address, value);
        return;
    }

    if (address >= 0xFF40 && address <= 0xFF4B) {
        if (ppu_) ppu_->write(address, value);
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
