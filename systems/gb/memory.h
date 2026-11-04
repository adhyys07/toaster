#pragma once

#include "cartridge.h"

#include <array>
#include <cstdint>
#include <string>

namespace emu::gb {

class APU;
class PPU;
class Timer;

// The address decoder. Registers whose writes have side effects live in the
// component that owns them -- the timer's divider, the PPU's line counter, the
// APU's channel triggers -- and are reached from here, so no component has to
// drive another one's hardware through a plain memory write.
class Memory {
public:
    Memory();

    void connect(Cartridge* cartridge);
    void connect(PPU* ppu, APU* apu, Timer* timer);
    void reset();

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);

    uint8_t joypad() const;
    void setButtons(uint8_t buttons);

    uint8_t interruptFlags() const;
    void requestInterrupt(uint8_t mask);
    void clearInterrupt(uint8_t mask);

    uint8_t interruptEnable() const;
    void setInterruptEnable(uint8_t value);

    // Video memory, for the PPU. Going through read() for every pixel fetch
    // would be both slow and wrong, since the decoder can block VRAM by mode.
    uint8_t* vram() { return vram_.data(); }
    const uint8_t* vram() const { return vram_.data(); }
    uint8_t* oam() { return oam_.data(); }
    const uint8_t* oam() const { return oam_.data(); }

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit.block(vram_.data(), vram_.size());
        visit.block(wram_.data(), wram_.size());
        visit.block(hram_.data(), hram_.size());
        visit.block(oam_.data(), oam_.size());
        visit(joypad_select_);
        visit(buttons_);
        visit(interrupt_flags_);
        visit(interrupt_enable_);
        visit(serial_data_);
        visit(serial_control_);
    }

    // Blargg's test ROMs report their results over the serial port, so the
    // bytes written there are collected rather than dropped.
    const std::string& serialOutput() const { return serial_output_; }
    void clearSerialOutput() { serial_output_.clear(); }

private:
    Cartridge* cartridge_ = nullptr;
    PPU* ppu_ = nullptr;
    APU* apu_ = nullptr;
    Timer* timer_ = nullptr;

    std::array<uint8_t, 0x2000> vram_{};
    std::array<uint8_t, 0x2000> wram_{};
    std::array<uint8_t, 0x7F> hram_{};
    std::array<uint8_t, 0xA0> oam_{};

    uint8_t joypad_select_ = 0x30;
    uint8_t buttons_ = 0xFF;

    uint8_t interrupt_flags_ = 0;
    uint8_t interrupt_enable_ = 0;

    uint8_t serial_data_ = 0;
    uint8_t serial_control_ = 0;
    std::string serial_output_;
};

} // namespace emu::gb
