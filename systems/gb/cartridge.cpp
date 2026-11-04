#include "cartridge.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace emu::gb {

namespace {

size_t decodeRamSize(uint8_t code) {
    switch (code) {
    case 0x00: return 0;
    case 0x01: return 2 * 1024;
    case 0x02: return 8 * 1024;
    case 0x03: return 32 * 1024;
    case 0x04: return 128 * 1024;
    case 0x05: return 64 * 1024;
    default: return 0;
    }
}

size_t decodeRomSize(uint8_t code) {
    if (code <= 8) {
        return static_cast<size_t>(32 * 1024) << code;
    }

    switch (code) {
    case 0x52: return 72 * 16 * 1024;
    case 0x53: return 80 * 16 * 1024;
    case 0x54: return 96 * 16 * 1024;
    default: return 0;
    }
}

std::string describeType(uint8_t type) {
    char text[8] = {};
    std::snprintf(text, sizeof(text), "0x%02X", type);
    return text;
}

std::filesystem::path batteryPath(const std::string& rom_path) {
    return std::filesystem::path(rom_path).replace_extension(".srm");
}

} // namespace

bool Cartridge::load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        error = "Could not open Game Boy ROM: " + path;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0x150) {
        error = "Game Boy ROM is too small";
        return false;
    }

    rom_.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(rom_.data()), size);

    if (!file) {
        error = "Could not read Game Boy ROM";
        rom_.clear();
        return false;
    }

    title_.clear();

    for (size_t i = 0x134; i <= 0x143 && rom_[i] != 0; ++i) {
        title_.push_back(static_cast<char>(rom_[i]));
    }

    const uint8_t cartridge_type = rom_[0x147];
    const uint8_t rom_size_code = rom_[0x148];
    const uint8_t ram_size_code = rom_[0x149];

    battery_ = false;
    has_rtc_ = false;

    switch (cartridge_type) {
    case 0x00: mapper_ = Mapper::RomOnly; break;
    case 0x08: mapper_ = Mapper::RomOnly; break;
    case 0x09: mapper_ = Mapper::RomOnly; battery_ = true; break;

    case 0x01:
    case 0x02: mapper_ = Mapper::Mbc1; break;
    case 0x03: mapper_ = Mapper::Mbc1; battery_ = true; break;

    case 0x05: mapper_ = Mapper::Mbc2; break;
    case 0x06: mapper_ = Mapper::Mbc2; battery_ = true; break;

    case 0x0F:
    case 0x10: mapper_ = Mapper::Mbc3; battery_ = true; has_rtc_ = true; break;
    case 0x11:
    case 0x12: mapper_ = Mapper::Mbc3; break;
    case 0x13: mapper_ = Mapper::Mbc3; battery_ = true; break;

    case 0x19:
    case 0x1A:
    case 0x1C:
    case 0x1D: mapper_ = Mapper::Mbc5; break;
    case 0x1B:
    case 0x1E: mapper_ = Mapper::Mbc5; battery_ = true; break;

    default:
        error = "Unsupported Game Boy cartridge type: " +
                describeType(cartridge_type);
        rom_.clear();
        return false;
    }

    const size_t expected_rom_size = decodeRomSize(rom_size_code);
    if (expected_rom_size != 0 && rom_.size() < expected_rom_size) {
        error = "Game Boy ROM is smaller than its header specifies";
        rom_.clear();
        return false;
    }

    // MBC2 has 512 half-bytes of RAM built into the mapper, and its header
    // reports no external RAM at all.
    ram_.assign(mapper_ == Mapper::Mbc2 ? 512 : decodeRamSize(ram_size_code), 0);

    loaded_ = true;
    reset();
    return true;
}

void Cartridge::reset() {
    ram_enabled_ = false;
    rom_bank_ = 1;
    ram_bank_ = 0;
    banking_mode_ = 0;
    rtc_select_ = 0;
    rtc_latch_ = 0xFF;
    std::fill(std::begin(rtc_), std::end(rtc_), uint8_t{0});
    std::fill(std::begin(rtc_latched_), std::end(rtc_latched_), uint8_t{0});
}

size_t Cartridge::romBankCount() const {
    return std::max<size_t>(1, rom_.size() / 0x4000);
}

size_t Cartridge::ramBankCount() const {
    return std::max<size_t>(1, ram_.size() / 0x2000);
}

size_t Cartridge::romOffset(uint16_t addr) const {
    size_t bank = 0;

    if (addr < 0x4000) {
        // In MBC1's advanced banking mode the high bits also select which
        // bank appears in the low half of the address space.
        if (mapper_ == Mapper::Mbc1 && banking_mode_ != 0) {
            bank = static_cast<size_t>(rom_bank_ & 0x60);
        }
        return (bank % romBankCount()) * 0x4000 + addr;
    }

    bank = rom_bank_;
    if (mapper_ == Mapper::Mbc1 || mapper_ == Mapper::Mbc2 ||
        mapper_ == Mapper::Mbc3) {
        // Only MBC5 can select bank 0 in the high half.
        if (bank == 0) bank = 1;
    }
    return (bank % romBankCount()) * 0x4000 +
           static_cast<size_t>(addr - 0x4000);
}

bool Cartridge::ramOffset(uint16_t addr, size_t& offset) const {
    if (ram_.empty()) return false;

    if (mapper_ == Mapper::Mbc2) {
        // MBC2 ignores the upper address bits and has no banks.
        offset = static_cast<size_t>(addr - 0xA000) & 0x1FF;
        return true;
    }

    size_t bank = ram_bank_;
    // MBC1 only reaches RAM banks in advanced banking mode.
    if (mapper_ == Mapper::Mbc1 && banking_mode_ == 0) bank = 0;

    offset = (bank % ramBankCount()) * 0x2000 +
             static_cast<size_t>(addr - 0xA000);
    return offset < ram_.size();
}

void Cartridge::latchRtc() {
    // A real MBC3 counts seconds continuously. Sampling the host clock on
    // latch keeps in-game clocks moving without a battery-backed counter.
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return;

    rtc_[0] = static_cast<uint8_t>(local->tm_sec);
    rtc_[1] = static_cast<uint8_t>(local->tm_min);
    rtc_[2] = static_cast<uint8_t>(local->tm_hour);
    rtc_[3] = static_cast<uint8_t>(local->tm_yday & 0xFF);
    rtc_[4] = static_cast<uint8_t>((local->tm_yday >> 8) & 0x01);

    std::copy(std::begin(rtc_), std::end(rtc_), std::begin(rtc_latched_));
}

uint8_t Cartridge::read(uint16_t addr) const {
    if (!loaded_) return 0xFF;

    if (addr < 0x8000) {
        const size_t offset = romOffset(addr);
        return offset < rom_.size() ? rom_[offset] : 0xFF;
    }

    if (addr >= 0xA000 && addr < 0xC000) {
        if (!ram_enabled_) return 0xFF;

        if (has_rtc_ && rtc_select_ >= 0x08 && rtc_select_ <= 0x0C) {
            return rtc_latched_[rtc_select_ - 0x08];
        }

        size_t offset = 0;
        if (!ramOffset(addr, offset)) return 0xFF;
        // MBC2 stores only the low nibble; the high bits read back as ones.
        return mapper_ == Mapper::Mbc2 ? static_cast<uint8_t>(ram_[offset] | 0xF0)
                                       : ram_[offset];
    }

    return 0xFF;
}

void Cartridge::write(uint16_t addr, uint8_t value) {
    if (!loaded_) return;

    if (addr < 0x2000) {
        if (mapper_ == Mapper::Mbc2) {
            // MBC2 decodes bit 8 of the address: clear selects RAM enable,
            // set selects the ROM bank register.
            if ((addr & 0x0100) == 0) ram_enabled_ = (value & 0x0F) == 0x0A;
            return;
        }
        if (mapper_ != Mapper::RomOnly) ram_enabled_ = (value & 0x0F) == 0x0A;
        return;
    }

    if (addr < 0x4000) {
        switch (mapper_) {
        case Mapper::Mbc1:
            rom_bank_ = static_cast<uint16_t>((rom_bank_ & 0x60) |
                                              (value & 0x1F));
            break;
        case Mapper::Mbc2:
            if (addr & 0x0100) {
                rom_bank_ = static_cast<uint16_t>(value & 0x0F);
            }
            break;
        case Mapper::Mbc3:
            rom_bank_ = static_cast<uint16_t>(value & 0x7F);
            break;
        case Mapper::Mbc5:
            if (addr < 0x3000) {
                rom_bank_ = static_cast<uint16_t>((rom_bank_ & 0x100) | value);
            } else {
                rom_bank_ = static_cast<uint16_t>((rom_bank_ & 0x0FF) |
                                                  ((value & 1) << 8));
            }
            break;
        case Mapper::RomOnly:
            break;
        }
        return;
    }

    if (addr < 0x6000) {
        switch (mapper_) {
        case Mapper::Mbc1:
            // The same register is RAM bank or the ROM bank's high bits,
            // depending on the banking mode.
            rom_bank_ = static_cast<uint16_t>((rom_bank_ & 0x1F) |
                                              ((value & 0x03) << 5));
            ram_bank_ = value & 0x03;
            break;
        case Mapper::Mbc3:
            if (value <= 0x03) {
                ram_bank_ = value;
                rtc_select_ = 0;
            } else if (value >= 0x08 && value <= 0x0C) {
                rtc_select_ = value;
            }
            break;
        case Mapper::Mbc5:
            ram_bank_ = value & 0x0F;
            break;
        case Mapper::Mbc2:
        case Mapper::RomOnly:
            break;
        }
        return;
    }

    if (addr < 0x8000) {
        if (mapper_ == Mapper::Mbc1) {
            banking_mode_ = value & 0x01;
        } else if (mapper_ == Mapper::Mbc3 && has_rtc_) {
            // Latching happens on a 0 then 1 write.
            if (rtc_latch_ == 0x00 && value == 0x01) latchRtc();
            rtc_latch_ = value;
        }
        return;
    }

    if (addr >= 0xA000 && addr < 0xC000) {
        if (!ram_enabled_) return;

        if (has_rtc_ && rtc_select_ >= 0x08 && rtc_select_ <= 0x0C) {
            rtc_[rtc_select_ - 0x08] = value;
            rtc_latched_[rtc_select_ - 0x08] = value;
            return;
        }

        size_t offset = 0;
        if (!ramOffset(addr, offset)) return;
        ram_[offset] = mapper_ == Mapper::Mbc2
            ? static_cast<uint8_t>(value & 0x0F)
            : value;
    }
}

bool Cartridge::loadBattery(const std::string& rom_path) {
    if (!battery_ || ram_.empty()) return true;

    std::ifstream file(batteryPath(rom_path), std::ios::binary);
    if (!file) return true;  // No save yet is not an error.

    file.read(reinterpret_cast<char*>(ram_.data()),
              static_cast<std::streamsize>(ram_.size()));
    return true;
}

bool Cartridge::saveBattery(const std::string& rom_path) const {
    if (!battery_ || ram_.empty()) return true;

    const auto path = batteryPath(rom_path);
    const auto temporary = path.string() + ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(ram_.data()),
                   static_cast<std::streamsize>(ram_.size()));
        if (!file) return false;
    }

    // Write to a temporary first so an interrupted save cannot destroy the
    // previous one.
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace emu::gb
