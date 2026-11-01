#include "cartridge.h"

#include <algorithm>
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

    switch (cartridge_type) {
    case 0x00:
        mapper_ = Mapper::RomOnly;
        break;

    case 0x01:
    case 0x02:
    case 0x03:
        mapper_ = Mapper::Mbc1;
        break;

    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        mapper_ = Mapper::Mbc3;
        break;

    default:
        error = "Unsupported Game Boy cartridge type: " +
                std::to_string(cartridge_type);
        rom_.clear();
        return false;
    }

    const size_t expected_rom_size = decodeRomSize(rom_size_code);
    if (expected_rom_size != 0 && rom_.size() < expected_rom_size) {
        error = "Game Boy ROM is smaller than its header specifies";
        rom_.clear();
        return false;
    }

    ram_.assign(decodeRamSize(ram_size_code), 0);
    loaded_ = true;
    reset();
    return true;
}

void Cartridge::reset() {
    ram_enabled_ = false;
    rom_bank_low_ = 1;
    bank_high_ = 0;
    banking_mode_ = 0;
}

size_t Cartridge::romBankCount() const {
    return std::max<size_t>(1, rom_.size() / 0x4000);
}

size_t Cartridge::ramBankCount() const {
    return std::max<size_t>(1, ram_.size() / 0x2000);
}

uint8_t Cartridge::read(uint16_t address) const {
    if (!loaded_) return 0xFF;

    if (address < 0x4000) {
        size_t bank = 0;

        if (mapper_ == Mapper::Mbc1 && banking_mode_ != 0) {
            bank = static_cast<size_t>(bank_high_) << 5;
        }

        bank %= romBankCount();

        const size_t offset =
            bank * 0x4000 + static_cast<size_t>(address);

        return offset < rom_.size() ? rom_[offset] : 0xFF;
    }

    if (address < 0x8000) {
        size_t bank = rom_bank_low_ & 0x1F;

        if (mapper_ == Mapper::Mbc1) {
            bank |= static_cast<size_t>(bank_high_) << 5;
        } else if (mapper_ == Mapper::Mbc3) {
            bank = rom_bank_low_;
        }

        if ((bank & 0x1F) == 0) {
            ++bank;
        }

        bank %= romBankCount();

        const size_t offset =
            bank * 0x4000 + static_cast<size_t>(address - 0x4000);

        return offset < rom_.size() ? rom_[offset] : 0xFF;
    }

    if (address >= 0xA000 && address < 0xC000) {
        if (!ram_enabled_ || ram_.empty()) return 0xFF;

        size_t bank = 0;

        if (mapper_ == Mapper::Mbc1 &&
            banking_mode_ != 0) {
            bank = bank_high_;
        }

        bank %= ramBankCount();

        const size_t offset =
            bank * 0x2000 + static_cast<size_t>(address - 0xA000);

        return offset < ram_.size() ? ram_[offset] : 0xFF;
    }

    return 0xFF;
}

void Cartridge::write(uint16_t address, uint8_t value) {
    if (!loaded_) return;

    if (address < 0x2000) {
        ram_enabled_ = (value & 0x0F) == 0x0A;
        return;
    }

    if (mapper_ == Mapper::RomOnly) {
        return;
    }

    if (address < 0x4000) {
        rom_bank_low_ = value & 0x1F;

        if (rom_bank_low_ == 0) {
            rom_bank_low_ = 1;
        }

        return;
    }

    if (address < 0x6000) {
        bank_high_ = value & 0x03;
        return;
    }

    if (address < 0x8000) {
        banking_mode_ = value & 1;
        return;
    }

    if (address >= 0xA000 && address < 0xC000 &&
        ram_enabled_ && !ram_.empty()) {
        size_t bank = 0;

        if (mapper_ == Mapper::Mbc1 &&
            banking_mode_ != 0) {
            bank = bank_high_;
        }

        bank %= ramBankCount();

        const size_t offset =
            bank * 0x2000 + static_cast<size_t>(address - 0xA000);

        if (offset < ram_.size()) {
            ram_[offset] = value;
        }
    }
}

} // namespace emu::gb