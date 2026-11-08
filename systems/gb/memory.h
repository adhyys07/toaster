#pragma once

#include "../../src/core/system.h"

#include "cartridge.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace emu::gb {

class APU;
class PPU;
class Timer;

class Memory {
public:
    Memory();

    void connect(Cartridge* cartridge);
    void connect(PPU* ppu, APU* apu, Timer* timer);
    void reset();

    uint8_t read(uint16_t address);
    uint8_t readRaw(uint16_t address);
    void write(uint16_t address, uint8_t value);

    void setColorMode(bool on) { color_ = on; }
    bool colorMode() const { return color_; }

    uint8_t* vramBank(int bank) {
        return vram_.data() + (bank & 1) * 0x2000;
    }
    const uint8_t* vramBank(int bank) const {
        return vram_.data() + (bank & 1) * 0x2000;
    }

    const uint8_t* bgPaletteRam() const { return bg_palette_.data(); }
    const uint8_t* objPaletteRam() const { return obj_palette_.data(); }

    bool doubleSpeed() const { return double_speed_; }
    void setDoubleSpeed(bool on) { double_speed_ = on; }
    bool speedSwitchArmed() const { return (key1_ & 0x01) != 0; }
    void clearSpeedSwitch() { key1_ &= 0x7E;  }

    uint8_t joypad() const;
    void setButtons(uint8_t buttons);

    void setPatches(std::vector<emu::MemoryPatch> patches) {
        patches_ = std::move(patches);
    }

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
        visit.block(bg_palette_.data(), bg_palette_.size());
        visit.block(obj_palette_.data(), obj_palette_.size());
        visit(vram_bank_);
        visit(wram_bank_);
        visit(bg_palette_index_);
        visit(obj_palette_index_);
        visit(key1_);
        visit(double_speed_);
        visit.block(hram_.data(), hram_.size());
        visit.block(oam_.data(), oam_.size());
        visit(joypad_select_);
        visit(buttons_);
        visit(interrupt_flags_);
        visit(interrupt_enable_);
        visit(serial_data_);
        visit(serial_control_);
    }

    using SerialExchange = std::function<uint8_t(uint8_t outgoing)>;
    void setSerialExchange(SerialExchange exchange) {
        serial_exchange_ = std::move(exchange);
    }
    bool linked() const { return static_cast<bool>(serial_exchange_); }

    // Blargg's test ROMs report their results over the serial port, so the
    // bytes written there are collected rather than dropped.
    const std::string& serialOutput() const { return serial_output_; }
    void clearSerialOutput() { serial_output_.clear(); }

private:
    uint8_t patch(uint16_t address, uint8_t value) const;

    std::vector<emu::MemoryPatch> patches_;

    Cartridge* cartridge_ = nullptr;
    PPU* ppu_ = nullptr;
    APU* apu_ = nullptr;
    Timer* timer_ = nullptr;

    std::array<uint8_t, 0x4000> vram_{};
    std::array<uint8_t, 0x8000> wram_{};
    std::array<uint8_t, 0x7F> hram_{};
    std::array<uint8_t, 0xA0> oam_{};

    std::array<uint8_t, 64> bg_palette_{};
    std::array<uint8_t, 64> obj_palette_{};

    bool color_ = false;

    uint8_t vram_bank_ = 0;
    uint8_t wram_bank_ = 1;
    uint8_t bg_palette_index_ = 0;
    uint8_t obj_palette_index_ = 0;
    uint8_t key1_ = 0;
    bool double_speed_ = false;

    uint8_t joypad_select_ = 0x30;
    uint8_t buttons_ = 0xFF;

    uint8_t interrupt_flags_ = 0;
    uint8_t interrupt_enable_ = 0;

    uint8_t serial_data_ = 0;
    uint8_t serial_control_ = 0;
    std::string serial_output_;
    // Not serialized: a save state must not carry a cable that is no
    // longer plugged in, and the partner has its own state anyway.
    SerialExchange serial_exchange_;
};

} // namespace emu::gb
