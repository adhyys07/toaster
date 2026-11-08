#include "ppu.h"

#include "memory.h"

#include <algorithm>

namespace emu::gb {
namespace {

// DMG panel colours, index 0 lightest.
void PPU::setColors(const uint32_t* colors) {
    for (int i = 0; i < 4; ++i) colors_[i] = colors[i];
}

// A palette register packs four 2-bit shades, low bits first.
inline uint8_t shadeOf(uint8_t palette, uint8_t pixel) {
    return static_cast<uint8_t>((palette >> (pixel * 2)) & 0x03);
}

constexpr int kDotsPerLine = 456;
constexpr int kOamScanDots = 80;
constexpr int kDrawDots = 172;
constexpr int kVisibleLines = 144;
constexpr int kTotalLines = 154;

} // namespace

PPU::PPU(Memory& memory)
    : memory_(memory) {
    reset();
}

void PPU::reset() {
    framebuffer_.fill(colors_[0]);
    line_pixels_.fill(0);

    lcdc_ = 0x91;
    stat_ = 0x85;
    scy_ = 0;
    scx_ = 0;
    ly_ = 0;
    lyc_ = 0;
    dma_ = 0xFF;
    bgp_ = 0xFC;
    obp0_ = 0xFF;
    obp1_ = 0xFF;
    wy_ = 0;
    wx_ = 0;

    dot_ = 0;
    mode_ = 2;
    window_line_ = 0;
    stat_line_ = false;
    frame_complete_ = false;
}

uint8_t PPU::tilePixel(uint16_t data_base, uint8_t tile, int x, int y,
                       bool signed_index) const {
    // The 0x8800 tile block addresses tiles as signed offsets from 0x9000.
    const int index = signed_index ? static_cast<int8_t>(tile)
                                   : static_cast<int>(tile);
    const int address = static_cast<int>(data_base) + index * 16 + y * 2;
    const int offset = address - 0x8000;
    if (offset < 0 || offset + 1 >= 0x2000) return 0;

    const uint8_t* vram = memory_.vram();
    const uint8_t low = vram[offset];
    const uint8_t high = vram[offset + 1];

    const int bit = 7 - x;
    return static_cast<uint8_t>(((high >> bit) & 1) << 1 | ((low >> bit) & 1));
}

void PPU::renderBackground() {
    if ((lcdc_ & 0x01) == 0) {
        // With the background off the line reads as colour 0, which also
        // means it never blocks a sprite.
        for (int x = 0; x < 160; ++x) {
            line_pixels_[x] = 0;
            framebuffer_[ly_ * 160 + x] = colors_[0];
        }
        return;
    }

    const uint16_t map_base = (lcdc_ & 0x08) ? 0x9C00 : 0x9800;
    const bool signed_index = (lcdc_ & 0x10) == 0;
    const uint16_t data_base = signed_index ? 0x9000 : 0x8000;

    const int world_y = (ly_ + scy_) & 0xFF;
    const int tile_row = world_y / 8;
    const int pixel_y = world_y & 7;
    const uint8_t* vram = memory_.vram();

    for (int x = 0; x < 160; ++x) {
        const int world_x = (x + scx_) & 0xFF;
        const uint8_t tile =
            vram[map_base - 0x8000 + tile_row * 32 + world_x / 8];
        const uint8_t pixel =
            tilePixel(data_base, tile, world_x & 7, pixel_y, signed_index);

        line_pixels_[x] = pixel;
        framebuffer_[ly_ * 160 + x] = colors_[shadeOf(bgp_, pixel)];
    }
}

void PPU::renderWindow() {
    if ((lcdc_ & 0x20) == 0) return;          // window disabled
    if (ly_ < wy_) return;

    const int window_x = static_cast<int>(wx_) - 7;
    if (window_x >= 160) return;

    const uint16_t map_base = (lcdc_ & 0x40) ? 0x9C00 : 0x9800;
    const bool signed_index = (lcdc_ & 0x10) == 0;
    const uint16_t data_base = signed_index ? 0x9000 : 0x8000;

    const int tile_row = window_line_ / 8;
    const int pixel_y = window_line_ & 7;
    const uint8_t* vram = memory_.vram();

    for (int x = std::max(0, window_x); x < 160; ++x) {
        const int window_pixel = x - window_x;
        const uint8_t tile =
            vram[map_base - 0x8000 + tile_row * 32 + window_pixel / 8];
        const uint8_t pixel = tilePixel(data_base, tile, window_pixel & 7,
                                        pixel_y, signed_index);

        line_pixels_[x] = pixel;
        framebuffer_[ly_ * 160 + x] = colors_[shadeOf(bgp_, pixel)];
    }

    ++window_line_;
}

void PPU::renderSprites() {
    if ((lcdc_ & 0x02) == 0) return;

    const int height = (lcdc_ & 0x04) ? 16 : 8;
    const uint8_t* oam = memory_.oam();

    // Hardware fetches at most ten sprites per line, taken in OAM order.
    struct Candidate {
        int x;
        int index;
    };
    std::array<Candidate, 10> visible{};
    int count = 0;

    for (int index = 0; index < 40 && count < 10; ++index) {
        const int sprite_y = static_cast<int>(oam[index * 4]) - 16;
        if (ly_ < sprite_y || ly_ >= sprite_y + height) continue;
        visible[count++] = {static_cast<int>(oam[index * 4 + 1]) - 8, index};
    }

    // On DMG the smaller X wins, with ties going to the lower OAM index.
    // Drawing in reverse priority lets later writes cover earlier ones.
    std::stable_sort(visible.begin(), visible.begin() + count,
                     [](const Candidate& a, const Candidate& b) {
                         if (a.x != b.x) return a.x > b.x;
                         return a.index > b.index;
                     });

    for (int slot = 0; slot < count; ++slot) {
        const int index = visible[slot].index;
        const int sprite_y = static_cast<int>(oam[index * 4]) - 16;
        const int sprite_x = visible[slot].x;
        uint8_t tile = oam[index * 4 + 2];
        const uint8_t attributes = oam[index * 4 + 3];

        const bool behind_background = (attributes & 0x80) != 0;
        const bool flip_y = (attributes & 0x40) != 0;
        const bool flip_x = (attributes & 0x20) != 0;
        const uint8_t palette = (attributes & 0x10) ? obp1_ : obp0_;

        int row = ly_ - sprite_y;
        if (flip_y) row = height - 1 - row;
        if (height == 16) {
            // Tall sprites ignore bit 0 and span two consecutive tiles.
            tile = static_cast<uint8_t>(tile & 0xFE);
            if (row >= 8) {
                tile = static_cast<uint8_t>(tile | 0x01);
                row -= 8;
            }
        }

        for (int column = 0; column < 8; ++column) {
            const int x = sprite_x + column;
            if (x < 0 || x >= 160) continue;

            const int source_x = flip_x ? 7 - column : column;
            const uint8_t pixel = tilePixel(0x8000, tile, source_x, row, false);
            if (pixel == 0) continue;                  // colour 0 is clear
            if (behind_background && line_pixels_[x] != 0) continue;

            framebuffer_[ly_ * 160 + x] = colors_[shadeOf(palette, pixel)];
        }
    }
}

void PPU::renderScanLine() {
    if (ly_ >= kVisibleLines) return;

    renderBackground();
    renderWindow();
    renderSprites();
}

void PPU::setMode(uint8_t mode) {
    mode_ = mode & 0x03;
    updateStatLine();
}

void PPU::updateStatLine() {
    stat_ = static_cast<uint8_t>((stat_ & 0xF8) | mode_);
    if (ly_ == lyc_) {
        stat_ |= 0x04;
    } else {
        stat_ = static_cast<uint8_t>(stat_ & ~0x04);
    }

    // The STAT interrupt is level triggered: the request only fires as the
    // combined condition goes from false to true, so enabling two sources at
    // once does not produce two interrupts.
    const bool line =
        ((stat_ & 0x40) && ly_ == lyc_) ||
        ((stat_ & 0x20) && mode_ == 2) ||
        ((stat_ & 0x10) && mode_ == 1) ||
        ((stat_ & 0x08) && mode_ == 0);

    if (line && !stat_line_) memory_.requestInterrupt(0x02);
    stat_line_ = line;
}

void PPU::step() {
    // The dot is advanced first so that after n ticks the controller is at dot
    // n, and the mode a program reads back is the one for the dot it is in.
    if (++dot_ >= kDotsPerLine) {
        dot_ = 0;
        ++ly_;
        if (ly_ >= kTotalLines) {
            ly_ = 0;
            window_line_ = 0;
        }
        updateStatLine();   // LY changed, so LYC may now match
    }

    if (ly_ < kVisibleLines) {
        if (dot_ == 0) {
            setMode(2);
        } else if (dot_ == kOamScanDots) {
            setMode(3);
            renderScanLine();
        } else if (dot_ == kOamScanDots + kDrawDots) {
            setMode(0);
        }
    } else if (ly_ == kVisibleLines && dot_ == 0) {
        setMode(1);
        memory_.requestInterrupt(0x01);
        frame_complete_ = true;
    }
}

void PPU::tick(unsigned cycles) {
    if ((lcdc_ & 0x80) == 0) {
        // With the LCD off the controller is held in mode 0 at line 0 and the
        // panel is blank. Games switch it off to write VRAM freely.
        if (ly_ != 0 || mode_ != 0 || dot_ != 0) {
            ly_ = 0;
            dot_ = 0;
            window_line_ = 0;
            framebuffer_.fill(colors_[0]);
            setMode(0);
        }
        return;
    }

    for (unsigned i = 0; i < cycles; ++i) step();
}

void PPU::startOamDma(uint8_t page) {
    // 160 bytes from XX00 into OAM. Real hardware takes 160 machine cycles
    // and locks the bus; copying at once is accurate for anything that does
    // not race the transfer, which is the normal usage.
    const uint16_t source = static_cast<uint16_t>(page) << 8;
    uint8_t* oam = memory_.oam();
    for (uint16_t i = 0; i < 0xA0; ++i) {
        oam[i] = memory_.read(static_cast<uint16_t>(source + i));
    }
}

uint8_t PPU::read(uint16_t address) const {
    switch (address) {
    case 0xFF40: return lcdc_;
    // The top bit of STAT always reads as one.
    case 0xFF41: return static_cast<uint8_t>(stat_ | 0x80);
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly_;
    case 0xFF45: return lyc_;
    case 0xFF46: return dma_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    default:     return 0xFF;
    }
}

void PPU::write(uint16_t address, uint8_t value) {
    switch (address) {
    case 0xFF40: {
        const bool was_on = (lcdc_ & 0x80) != 0;
        lcdc_ = value;
        if (was_on && (lcdc_ & 0x80) == 0) {
            // Switching the LCD off resets the line counter immediately.
            ly_ = 0;
            dot_ = 0;
            window_line_ = 0;
            setMode(0);
        } else if (!was_on && (lcdc_ & 0x80) != 0) {
            ly_ = 0;
            dot_ = 0;
            window_line_ = 0;
            setMode(2);
        }
        break;
    }
    case 0xFF41:
        // Only the interrupt-enable bits are writable; mode and the coincidence
        // flag are owned by the controller.
        stat_ = static_cast<uint8_t>((stat_ & 0x07) | (value & 0x78));
        updateStatLine();
        break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF44:
        // LY is read-only on hardware; a write is ignored.
        break;
    case 0xFF45:
        lyc_ = value;
        updateStatLine();
        break;
    case 0xFF46:
        dma_ = value;
        startOamDma(value);
        break;
    case 0xFF47: bgp_ = value; break;
    case 0xFF48: obp0_ = value; break;
    case 0xFF49: obp1_ = value; break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    default: break;
    }
}

} // namespace emu::gb
