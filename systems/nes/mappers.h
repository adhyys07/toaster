#pragma once

#include "cartridge.h"

namespace nes {

class Mapper000 : public Mapper {
public:
    using Mapper::Mapper;
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
};

class Mapper001 : public Mapper {
public:
    Mapper001(uint8_t prg_banks, uint8_t chr_banks);
    void reset() override;
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
    bool overridesMirroring() const override { return true; }
    Mirroring mirroring() const override { return mirroring_; }

private:
    void writeRegister(uint16_t addr, uint8_t value);
    uint8_t shift_ = 0x10;
    uint8_t control_ = 0x1C;
    uint8_t chr_bank0_ = 0;
    uint8_t chr_bank1_ = 0;
    uint8_t prg_bank_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
};

class Mapper002 : public Mapper {
public:
    using Mapper::Mapper;
    void reset() override { bank_lo_ = 0; }
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;

private:
    uint8_t bank_lo_ = 0;
};

class Mapper003 : public Mapper {
public:
    using Mapper::Mapper;
    void reset() override { chr_bank_ = 0; }
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;

private:
    uint8_t chr_bank_ = 0;
};

class Mapper007 : public Mapper {
public:
    using Mapper::Mapper;
    void reset() override { prg_bank_ = 0; mirroring_ = Mirroring::OneScreenLo; }
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
    bool overridesMirroring() const override { return true; }
    Mirroring mirroring() const override { return mirroring_; }

private:
    uint8_t prg_bank_ = 0;
    Mirroring mirroring_ = Mirroring::OneScreenLo;
};

// MMC3 / TxROM. IRQ timing is clocked once per rendered scanline, which is an
// approximation of the hardware's PPU A12 edge behavior.
class Mapper004 : public Mapper {
public:
    using Mapper::Mapper;
    void reset() override;
    bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mapped) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mapped) override;
    void clockScanline() override;
    bool irqState() const override { return irq_pending_; }
    bool prgRamEnabled() const override { return ram_enabled_; }
    bool prgRamWritable() const override { return !ram_write_protected_; }
    bool overridesMirroring() const override { return true; }
    Mirroring mirroring() const override { return mirroring_; }

private:
    uint8_t bank_select_ = 0;
    uint8_t bank_registers_[8] = {0, 2, 4, 5, 6, 7, 0, 1};
    bool prg_mode_ = false;
    bool chr_invert_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    bool ram_enabled_ = true;
    bool ram_write_protected_ = false;
    uint8_t irq_latch_ = 0;
    uint8_t irq_counter_ = 0;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

}  // namespace nes
