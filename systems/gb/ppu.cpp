#include "ppu.h"

namespace emu::gb {

namespace {

constexpr uint32_t kColors[] = {
    0xFFE0F8D0u,
    0xFF88C070u,
    0xFF346856u,
    0xFF081820u
};

}

PPU::PPU(Memory& memory)
    : memory_(memory) {
    reset();
}

void PPU::reset() {
    framebuffer_.fill(kColors[0]);
    cycles_ = 0;
    line_ = 0;
}

uint8_t PPU::tilePixel(uint8_t tile, int x, int y) const {
    const uint16_t address =
        static_cast<uint16_t>(0x8000 + tile * 16 + y * 2);

    const uint8_t low = const_cast<Memory&>(memory_).read(address);
    const uint8_t high = const_cast<Memory&>(memory_).read(
        static_cast<uint16_t>(address + 1));

    const int bit = 7 - x;
    return static_cast<uint8_t>(
        ((high >> bit) & 1) << 1 | ((low >> bit) & 1));
}

void PPU::renderScanLine() {
    if (line_ >= 144) return;

    const uint8_t scroll_x = memory_.read(0xFF43);
    const uint8_t scroll_y = memory_.read(0xFF42);
    const uint8_t lcd_control = memory_.read(0xFF40);

    for (int x = 0; x < 160; ++x) {
        uint8_t shade = 0;

        if (lcd_control & 0x01) {
            const int world_x = (x + scroll_x) & 0xFF;
            const int world_y = (line_ + scroll_y) & 0xFF;

            const int tile_x = world_x / 8;
            const int tile_y = world_y / 8;

            const uint16_t map =
                (lcd_control & 0x08) ? 0x9C00 : 0x9800;

            const uint8_t tile = memory_.read(
                static_cast<uint16_t>(map + tile_y * 32 + tile_x));

            shade = tilePixel(tile, world_x & 7, world_y & 7);
        }

        framebuffer_[line_ * 160 + x] = kColors[shade];
    }
}   

void PPU::tick(unsigned cycles) {
    cycles_ += cycles;

    while (cycles_ >= 456) {
        cycles_ -= 456;

        if (line_ < 144) {
            renderScanLine();
        }

        ++line_;

        if (line_ == 144) {
            memory_.requestInterrupt(0x01);
        }

        if (line_ >= 154) {
            line_ = 0;
        }

        memory_.write(0xFF44, static_cast<uint8_t>(line_));

        const uint8_t status = memory_.read(0xFF41);
        const uint8_t mode = line_ >= 144 ? 1 : 2;
        memory_.write(0xFF41, static_cast<uint8_t>(
            (status & 0xFC) | mode));

        if (memory_.read(0xFF45) == static_cast<uint8_t>(line_)) {
            memory_.write(0xFF41, static_cast<uint8_t>(
                memory_.read(0xFF41) | 0x04));
        }
    }
} 

}