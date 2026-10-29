#include "cartridge.h";
#include "mappers.h";

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
    mirroring_ = Mirroring::Horizontal
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
    case 3: prg_bank_  = value & 0x0F; break;F
    }
}

bool Mapper001::cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t&){
    if (addr< 0x8000) return false;

    const uint8_t prg_mode = (control >> 2) & 0x03;

    if (prg_mode <= 1) {
        const uint32_t bank = (prg_bank & 0x0E) >> 1;
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

bool Mapper

}