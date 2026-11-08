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

    vram_bank_ = 0;
    wram_bank_ = 1;
    bg_palette_index_ = 0;
    obj_palette_index_ = 0;
    key1_ = 0;
    double_speed_ = false;
    // White rather than zero, so a game that enables the LCD before
    // uploading a palette shows a blank screen, not a black one.
    bg_palette_.fill(0xFF);
    obj_palette_.fill(0xFF);
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

uint8_t Memory::patch(uint16_t address, uint8_t value) const {
    for (const emu::MemoryPatch& item : patches_) {
        if (item.address != address) continue;
        if (item.has_compare && item.compare != value) continue;
        return item.value;
    }
    return value;
}

uint8_t Memory::read(uint16_t address) {
    if (!patches_.empty()) {
        // Only the cartridge and work RAM are worth patching; the register
        // file is hardware state rather than game data.
        if (address < 0x8000 || (address >= 0xA000 && address < 0xE000)) {
            return patch(address, readRaw(address));
        }
    }
    return readRaw(address);
}

uint8_t Memory::readRaw(uint16_t address) {
    if (address < 0x8000) {
        return cartridge_ ? cartridge_->read(address) : 0xFF;
    }

    if (address < 0xA000) {
        return vram_[(address - 0x8000) +
                     static_cast<std::size_t>(vram_bank_) * 0x2000];
    }

    if (address < 0xC000) {
        return cartridge_ ? cartridge_->read(address) : 0xFF;
    }

    if (address < 0xD000) {
        return wram_[address - 0xC000];
    }

    if (address < 0xE000) {
        // Banks 1-7 live here. Bank 0 is not selectable, so a written
        // 0 reads back as bank 1.
        const unsigned bank = wram_bank_ == 0 ? 1u : wram_bank_;
        return wram_[(address - 0xD000) + bank * 0x1000];
    }

    if (address < 0xFE00) {
        // Echo RAM mirrors C000-DDFF, banking included. It re-enters
        // readRaw rather than read, because this path is also what the
        // memory scanner uses and must stay free of cheat patches.
        return readRaw(static_cast<uint16_t>(address - 0x2000));
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

    // CGB registers. On a DMG cartridge they read as unconnected, so a
    // DMG game cannot accidentally observe colour hardware.
    if (address == 0xFF4F) return color_ ? (0xFE | vram_bank_) : 0xFF;
    if (address == 0xFF70) return color_ ? (0xF8 | wram_bank_) : 0xFF;
    if (address == 0xFF4D) {
        if (!color_) return 0xFF;
        return static_cast<uint8_t>((double_speed_ ? 0x80 : 0x00) |
                                    (key1_ & 0x01) | 0x7E);
    }
    if (address == 0xFF68) return bg_palette_index_;
    if (address == 0xFF69) return bg_palette_[bg_palette_index_ & 0x3F];
    if (address == 0xFF6A) return obj_palette_index_;
    if (address == 0xFF6B) return obj_palette_[obj_palette_index_ & 0x3F];

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
        vram_[(address - 0x8000) +
              static_cast<std::size_t>(vram_bank_) * 0x2000] = value;
        return;
    }

    if (address < 0xC000) {
        if (cartridge_) cartridge_->write(address, value);
        return;
    }

    if (address < 0xD000) {
        wram_[address - 0xC000] = value;
        return;
    }

    if (address < 0xE000) {
        const unsigned bank = wram_bank_ == 0 ? 1u : wram_bank_;
        wram_[(address - 0xD000) + bank * 0x1000] = value;
        return;
    }

    if (address < 0xFE00) {
        write(static_cast<uint16_t>(address - 0x2000), value);
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
        // Bit 7 starts a transfer, bit 0 selects the internal clock. Only
        // the clock master drives an exchange; a device on the external
        // clock waits to be driven.
        if ((value & 0x81) == 0x81) {
            if (serial_output_.size() < 4096) {
                serial_output_.push_back(static_cast<char>(serial_data_));
            }
            // A real cable shifts eight bits over eight clock periods.
            // Exchanging the whole byte at once is accurate enough for
            // trading and battling, which only care about byte order, and
            // it avoids running a second clock domain.
            serial_data_ = serial_exchange_ ? serial_exchange_(serial_data_)
                                            : 0xFF;
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

    if (address == 0xFF4F) {                  // VBK
        if (color_) vram_bank_ = value & 0x01;
        return;
    }
    if (address == 0xFF70) {                  // SVBK
        if (color_) wram_bank_ = value & 0x07;
        return;
    }
    if (address == 0xFF4D) {                  // KEY1
        // Writing bit 0 only arms the switch; the CPU performs it on
        // the next STOP, which is where double_speed_ actually flips.
        if (color_) key1_ = (key1_ & 0x80) | (value & 0x01);
        return;
    }
    if (address == 0xFF68) { bg_palette_index_ = value; return; }
    if (address == 0xFF69) {
        bg_palette_[bg_palette_index_ & 0x3F] = value;
        // Bit 7 auto-increments, which is how a game uploads a whole
        // palette through a single port.
        if (bg_palette_index_ & 0x80) {
            bg_palette_index_ =
                static_cast<uint8_t>(0x80 | ((bg_palette_index_ + 1) & 0x3F));
        }
        return;
    }
    if (address == 0xFF6A) { obj_palette_index_ = value; return; }
    if (address == 0xFF6B) {
        obj_palette_[obj_palette_index_ & 0x3F] = value;
        if (obj_palette_index_ & 0x80) {
            obj_palette_index_ =
                static_cast<uint8_t>(0x80 | ((obj_palette_index_ + 1) & 0x3F));
        }
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
