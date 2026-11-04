#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::gb {

class Cartridge {
public:
    template <typename Visitor>
    void visitState(Visitor& visit) {
        if (!ram_.empty()) visit.block(ram_.data(), ram_.size());
        visit(ram_enabled_); visit(rom_bank_low_);
        visit(bank_high_); visit(banking_mode_);
    }

    bool load(const std::string& path, std::string& error);

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    void reset();

    const std::string& title() const { return title_; }
    bool loaded() const { return loaded_; }

private:
    enum class Mapper {
        RomOnly,
        Mbc1,
        Mbc3
    };

    size_t romBankCount() const;
    size_t ramBankCount() const;

    std::vector<uint8_t> rom_;
    std::vector<uint8_t> ram_;

    std::string title_;
    Mapper mapper_ = Mapper::RomOnly;
    bool loaded_ = false;
    bool ram_enabled_ = false;

    uint8_t rom_bank_low_ = 1;
    uint8_t bank_high_ = 0;
    uint8_t banking_mode_ = 0;
};

}