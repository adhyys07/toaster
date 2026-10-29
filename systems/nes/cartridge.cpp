#include "cartridge.h"
#include "mappers.h"

#include <cstring>
#include <fstream>

namespace nes {
bool Mapper000::cpuMapRead(uint16_t addr, uint32_t&mapped, uint8_t&) {
    if (addr < 0x8000) return false;
    mapped = addr & (prg_banks_ > 1 ? 0x7FFF : 0x3FFF);
    return true;
    }

bool Mapper000::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t) {
    if (addr < 0x8000) return false;
    mapped = addr & (prg_banks_ > 1 ? 0x7FFF : 0x3FFF);
    return true;
}

bool Mapper000::ppuMapRead(uint16_t addr, uint32_t& mapped){
    if (addr > 0x1FFF) return false;
    mapped = addr;
    return true;
}

bool Mapper000::ppuMapWrite(uint16_t addr, uint32_t& mapped){
    if (addr > 0x1FFF) return false;
    mapped = addr;
    return chr_banks_ == 0;
}

Mapper001::Mapper001(uint8_t prg_banks, uint8_t chr_banks)
    : Mapper(prg_banks, chr_banks) {
    reset();
}

void Mapper001::reset(){
    shift_     = 0x10;
    control_   = 0x1C;
    chr_bank0_ = 0;
    chr_bank1_ = 0;
    prg_bank_  = 0;
    mirroring_ = Mirroring::Horizontal;
}

void Mapper001::writeRegister(uint16_t addr, uint8_t value) {
    switch ((addr >> 13) & 0x03) {
    case 0:
        control_ = value & 0x1F;
        switch (control_ & 0x03) {
        case 0: mirroring_ = Mirroring::OneScreenLo; break;
        case 1: mirroring_ = Mirroring::OneScreenHi; break;
        case 2: mirroring_ = Mirroring::Vertical;    break;
        case 3: mirroring_ = Mirroring::Horizontal;  break;
        }
        break;
    case 1: chr_bank0_ = value & 0x1F; break;
    case 2: chr_bank1_ = value & 0x1F; break;
    case 3: prg_bank_  = value & 0x0F; break;
    }
}

bool Mapper001::cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t&){
    if (addr< 0x8000) return false;

    const uint8_t prg_mode = (control_ >> 2) & 0x03;

    if (prg_mode <= 1) {
        const uint32_t bank = (prg_bank_ & 0x0E) >> 1;
        mapped = bank * 0x8000u + (addr & 0x7FFF);
    } else if (prg_mode == 2) {
        if (addr < 0xC000) mapped = addr & 0x3FFF;
        else               mapped = prg_bank_ * 0x4000u + (addr & 0x3FFF);
    } else {
        if (addr < 0xC000) mapped = prg_bank_ * 0x4000u + (addr & 0x3FFF);
        else               mapped = (prg_banks_ - 1) * 0x4000u + (addr & 0x3FFF);
    }
    return true;
}
bool Mapper001::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    if (addr < 0x8000) return false;
    mapped = kMapperHandled;

    if (data & 0x80) {
        shift_   = 0x10;
        control_ |= 0x0C;
        return true;
    }

    const bool complete = (shift_ & 0x01) != 0;  // bit walked all the way down
    shift_ = static_cast<uint8_t>((shift_ >> 1) | ((data & 0x01) << 4));

    if (complete) {
        writeRegister(addr, shift_);
        shift_ = 0x10;
    }
    return true;
}

bool Mapper001::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;

    if (chr_banks_ == 0) {
        mapped = addr;
        return true;
    }
    if (control_ & 0x10) {
        if (addr < 0x1000) mapped = chr_bank0_ * 0x1000u + (addr & 0x0FFF);
        else               mapped = chr_bank1_ * 0x1000u + (addr & 0x0FFF);
    } else {
        mapped = (chr_bank0_ & 0x1E) * 0x1000u + (addr & 0x1FFF);
    }
    return true;
}

bool Mapper001::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF || chr_banks_ != 0) return false;
    mapped = addr;
    return true;
}

bool Mapper002::cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t&) {
    if (addr < 0x8000) return false;
    if (addr < 0xC000) mapped = bank_lo_ * 0x4000u + (addr & 0x3FFF);
    else               mapped = (prg_banks_ - 1) * 0x4000u + (addr & 0x3FFF);
    return true;
}

bool Mapper002::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    if (addr < 0x8000) return false;
    bank_lo_ = data & 0x0F;
    mapped = kMapperHandled;
    return true;
}

bool Mapper002::ppuMapRead(uint16_t addr, uint32_t& mapped) {
        if (addr > 0x1FFF) return false;
    mapped = addr;
    return true;
}

bool Mapper002::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = addr;
    return chr_banks_ == 0;
}

bool Mapper003::cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t&) {
    if (addr < 0x8000) return false;
    mapped = addr & (prg_banks_ > 1 ? 0x7FFF : 0x3FFF);
    return true;
}

bool Mapper003::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    if (addr < 0x8000) return false;
    chr_bank_ = data & 0x03;
    mapped = kMapperHandled;
    return true;
}

bool Mapper003::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = chr_bank_ * 0x2000u + addr;
    return true;
}

bool Mapper003::ppuMapWrite(uint16_t, uint32_t&) { return false; }

bool Mapper007::cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t&) {
    if (addr < 0x8000) return false;
    mapped = prg_bank_ * 0x8000u + (addr & 0x7FFF);
    return true;
}

bool Mapper007::cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) {
    if (addr < 0x8000) return false;
    prg_bank_  = data & 0x07;
    mirroring_ = (data & 0x10) ? Mirroring::OneScreenHi : Mirroring::OneScreenLo;
    mapped = kMapperHandled;
    return true;
}

bool Mapper007::ppuMapRead(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = addr;
    return true;
}

bool Mapper007::ppuMapWrite(uint16_t addr, uint32_t& mapped) {
    if (addr > 0x1FFF) return false;
    mapped = addr;
    return true;
}

bool Cartridge::load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }

    struct Header {
        char    magic[4];
        uint8_t prg_chunks;   // x 16 KB
        uint8_t chr_chunks;   // x  8 KB
        uint8_t flags6;
        uint8_t flags7;
        uint8_t prg_ram_size;
        uint8_t tv_system1;
        uint8_t tv_system2;
        char    padding[5];
    } header{};

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || std::memcmp(header.magic, "NES\x1A", 4) != 0) {
        error = "not an iNES file (bad magic) — is this really a .nes ROM?";
        return false;
    }

    if (header.flags6 & 0x04) file.seekg(512, std::ios::cur);

    mapper_id_ = ((header.flags7 >> 4) << 4) | (header.flags6 >> 4);

    if (header.flags6 & 0x08)      mirroring_ = Mirroring::FourScreen;
    else if (header.flags6 & 0x01) mirroring_ = Mirroring::Vertical;
    else                           mirroring_ = Mirroring::Horizontal;

    if (header.prg_chunks == 0) {
        error = "header claims zero PRG banks";
        return false;
    }

    prg_.resize(header.prg_chunks * 16384u);
    file.read(reinterpret_cast<char*>(prg_.data()),
              static_cast<std::streamsize>(prg_.size()));
    if (!file) {
        error = "file is truncated — PRG data is shorter than the header claims";
        return false;
    }

    if (header.chr_chunks == 0) {
        chr_is_ram_ = true;
        chr_.assign(8192, 0);
    } else {
        chr_.resize(header.chr_chunks * 8192u);
        file.read(reinterpret_cast<char*>(chr_.data()),
                  static_cast<std::streamsize>(chr_.size()));
        if (!file) {
            error = "file is truncated — CHR data is shorter than the header claims";
            return false;
        }
    }

    prg_ram_.assign(8192, 0);

    switch (mapper_id_) {
    case 0: mapper_ = std::make_unique<Mapper000>(header.prg_chunks, header.chr_chunks); break;
    case 1: mapper_ = std::make_unique<Mapper001>(header.prg_chunks, header.chr_chunks); break;
    case 2: mapper_ = std::make_unique<Mapper002>(header.prg_chunks, header.chr_chunks); break;
    case 3: mapper_ = std::make_unique<Mapper003>(header.prg_chunks, header.chr_chunks); break;
    case 7: mapper_ = std::make_unique<Mapper007>(header.prg_chunks, header.chr_chunks); break;
    default:
        error = "mapper " + std::to_string(mapper_id_) +
                " is not implemented (this build supports 0, 1, 2, 3 and 7)";
        return false;
    }

    mapper_->reset();
    return true;
}

void Cartridge::reset() {
    if (mapper_) mapper_->reset();
}

Mirroring Cartridge::mirroring() const {
    if (mapper_ && mapper_->overridesMirroring()) return mapper_->mirroring();
    return mirroring_;
}

bool Cartridge::cpuRead(uint16_t addr, uint8_t& data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        data = prg_ram_[addr & 0x1FFF];
        return true;
    }
    if (!mapper_) return false;

    uint32_t mapped = 0;
    if (mapper_->cpuMapRead(addr, mapped, data)) {
        if (mapped == kMapperHandled) return true;
        if (mapped < prg_.size()) {
            data = prg_[mapped];
            return true;
        }
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        prg_ram_[addr & 0x1FFF] = data;
        return true;
    }
    if (!mapper_) return false;

    uint32_t mapped = 0;
    if (mapper_->cpuMapWrite(addr, mapped, data)) {
        if (mapped == kMapperHandled) return true;
        if (mapped < prg_.size()) return true;
    }
    return false;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& data) {
    if (!mapper_) return false;
    uint32_t mapped = 0;
    if (mapper_->ppuMapRead(addr, mapped) && mapped < chr_.size()) {
        data = chr_[mapped];
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t data) {
    if (!mapper_) return false;
    uint32_t mapped = 0;
    if (mapper_->ppuMapWrite(addr, mapped) && mapped < chr_.size()) {
        chr_[mapped] = data;
        return true;
    }
    return false;
}

}
