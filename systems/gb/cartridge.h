#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::gb {

class Cartridge {
public:
    bool load(const std::string& path, std::string& error);

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    void reset();

    const std::string& title() const { return title_; }
    bool loaded() const { return loaded_; }
    bool hasBattery() const { return battery_; }

    bool isColor() const { return color_; }

    // Cartridge RAM persists across runs for battery-backed carts. Both are
    // no-ops for a cart without a battery.
    bool loadBattery(const std::string& rom_path);
    bool saveBattery(const std::string& rom_path) const;

    template <typename Visitor>
    void visitState(Visitor& visit) {
        if (!ram_.empty()) visit.block(ram_.data(), ram_.size());
        visit(ram_enabled_);
        visit(rom_bank_);
        visit(ram_bank_);
        visit(banking_mode_);
        visit(rtc_select_);
        visit(rtc_latch_);
        visit.block(rtc_, sizeof(rtc_));
        visit.block(rtc_latched_, sizeof(rtc_latched_));
    }

private:
    enum class Mapper {
        RomOnly,
        Mbc1,
        Mbc2,
        Mbc3,
        Mbc5
    };

    size_t romBankCount() const;
    size_t ramBankCount() const;
    size_t romOffset(uint16_t addr) const;
    bool ramOffset(uint16_t addr, size_t& offset) const;
    void latchRtc();

    std::vector<uint8_t> rom_;
    std::vector<uint8_t> ram_;

    std::string title_;
    Mapper mapper_ = Mapper::RomOnly;
    bool loaded_ = false;
    bool battery_ = false;
    bool has_rtc_ = false;

    bool color_ = false;

    bool ram_enabled_ = false;
    // MBC5 reaches 512 banks, so this does not fit in a byte.
    uint16_t rom_bank_ = 1;
    uint8_t ram_bank_ = 0;
    uint8_t banking_mode_ = 0;

    // MBC3's clock. rtc_ runs; rtc_latched_ is what the game reads.
    uint8_t rtc_select_ = 0;
    uint8_t rtc_latch_ = 0xFF;
    uint8_t rtc_[5] = {};
    uint8_t rtc_latched_[5] = {};
};

} // namespace emu::gb
