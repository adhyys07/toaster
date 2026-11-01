#pragma once

#include "memory.h"
#include <array>
#include <cstdint>

namespace emu::gb {

class PPU {
public:
    explicit PPU(Memory& memory);

    void reset();
    void tick(unsigned cycles);

    const uint32_t* framebuffer() const { return framebuffer_.data(); }

private:
    void renderScanLine();
    uint8_t tilePixel(uint8_t tile, int x, int y) const;

    Memory& memory_;
    std::array<uint32_t, 160 * 144> framebuffer_{};

    unsigned cycles_ = 0;
    int line_ = 0;
};

}