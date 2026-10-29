#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
 
namespace nes {
    Horizontal,
    Vertical,
    OneScreenLo,
    OneScreenHi,
    FourScreen,   
};

constexpr uint32_t kMapperHandled = 0xFFFFFFFFu;

class Mapper {
public:
    Mapper(uint8_t prg_banks, uint8_t chr_banks)
        : prg_banks_(prg_banks), chr_banks_(chr_banks) {}
    virtual ~Mapper() = default;
 
    virtual void reset() {}

    virtual bool cpuMapRead(uint16_t addr, uint32_t& mapped, uint8_t& data) = 0;
    virtual bool cpuMapWrite(uint16_t addr, uint32_t& mapped, uint8_t data) = 0;
    virtual bool ppuMapRead(uint16_t addr, uint32_t& mapped) = 0;
    virtual bool ppuMapWrite(uint16_t addr, uint32_t& mapped) = 0;

    virtual bool overridesMirroring() const { return false; }
    virtual Mirroring mirroring() const { return Mirroring::Horizontal; }
 
protected:
    uint8_t prg_banks_ = 0;  // in 16 KB units
    uint8_t chr_banks_ = 0;  // in 8 KB units
};
 

class Cartridge {
public:
    bool load(const std::string& error);
    void reset();

    bool cpuRead(uint16_t addr, uint8_t& data);
    bool cpuWrite(uint16_t addr, uint8_t data);
    bool ppuRead(uint16_t addr, uint8_t& data);
    bool ppuWrite(uint16_t addr, uint8_t data);
 
    Mirroring mirroring() const;
 
    int  mapperId()   const { return mapper_id_; }
    bool loaded()     const { return mapper_ != nullptr; }
    size_t prgSize()  const { return prg_.size(); }
    size_t chrSize()  const { return chr_.size(); }
    bool chrIsRam()   const { return chr_is_ram_; 

private:
        std::vector<uint8_t> prg_:
        std::vector<uint8_t> chr_:
        std::vector<uint8_t> prg_ram_;  // $6000-$7FFF battery/work RAM
 
        std::unique_ptr<Mapper> mapper_;
        Mirroring mirroring_  = Mirroring::Horizontal;
        int       mapper_id_  = 0;
        bool      chr_is_ram_ = false;
};
}