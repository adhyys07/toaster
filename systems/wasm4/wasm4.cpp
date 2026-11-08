#include "wasm4.h"

#include "font.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <cstdio>

namespace emu::wasm4 {
namespace {

constexpr uint32_t kDefaultPalette[4] = {
    0xFFE0F8CFu, 0xFF86C06Cu, 0xFF306850u, 0xFF071821u
};

uint32_t readLe32(const uint8_t* memory, uint32_t address) {
    return static_cast<uint32_t>(memory[address]) |
           (static_cast<uint32_t>(memory[address + 1]) << 8) |
           (static_cast<uint32_t>(memory[address + 2]) << 16) |
           (static_cast<uint32_t>(memory[address + 3]) << 24);
}

}

void Console::reset() {
    memory_.fill(0);
    for (int i = 0; i < 4; ++i) {
        const uint32_t colour = kDefaultPalette[i];
        const uint32_t at = kPalette + i * 4;
        memory_[at + 0] = static_cast<uint8_t>(colour & 0xFF);
        memory_[at + 1] = static_cast<uint8_t>((colour >> 8) & 0xFF);
        memory_[at + 2] = static_cast<uint8_t>((colour >> 16) & 0xFF);
        memory_[at + 3] = static_cast<uint8_t>((colour >> 24) & 0xFF);
    }
    memory_[kDrawColors] = 0x03;
    memory_[kDrawColors + 1] = 0x12;
    started_ = false;
}

bool Console::loadRom(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Could not open " + path;
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        error = path + " is empty.";
        return false;
    }
    file.seekg(0, std::ios::beg);
    cartridge_.assign(static_cast<std::size_t>(size), 0);
    if (!file.read(reinterpret_cast<char*>(cartridge_.data()), size)) {
        error = "Could not read " + path;
        return false;
    }

    if (cartridge_.size() < 4 || cartridge_[0] != 0x00 ||
        cartridge_[1] != 'a' || cartridge_[2] != 's' ||
        cartridge_[3] != 'm') {
        error = path + " is not a WebAssembly module.";
        return false;
    }

    reset();
    runtime_ = makeWasm3Runtime();
    if (!runtime_) {
        error = "This build has no WebAssembly runtime.";
        return false;
    }
    return runtime_->load(cartridge_, memory_.data(), error);
}


void Console::runFrame() {
    if (!runtime_ || runtime_ -> ready()) return;

    if (!started_) {
        runtime_->callStart();
        started_ = true;
    }
    runtime_->callUpdate();
    present();
}

void Console::present() {
    uint32_t palette[4];
    for (int i = 0; i < 4; ++i) {
        palette[i] = 0xFF000000u |
                     (readLe32(memory_.data(), kPalette + i * 4) & 0x00FFFFFFu);
    }

    for (int i = 0; i < kScreenWidth * kScreenHeight; ++i) {
        const uint8_t packed = memory_[kFramebuffer + i / 4];
        const int shift = (i % 4) * 2;
        framebuffer_[i] = palette[(packed >> shift) & 0x03];
    }
}

void Console::setPixel(int32_t x, int32_t y, uint8_t colour) {
    if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return;
    const int index = y * kScreenWidth + x;
    const uint32_t at = kFramebuffer + index / 4;
    const int shift = (index % 4) * 2;
    memory_[at] = static_cast<uint8_t>((memory_[at] & ~(0x03 << shift)) |
                                       ((colour & 0x03) << shift));
}

uint8_t Console::drawColor(int index) const {
    const uint16_t colors = static_cast<uint16_t>(
        memory_[kDrawColors] | (memory_[kDrawColors + 1] << 8));
    return static_cast<uint8_t>((colors >> (index * 4)) & 0x0F);
}


void Console::rect(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    const uint8_t fill = drawColor(0);
    const uint8_t stroke = drawColor(1);

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const bool edge = row == 0 || col == 0 ||
                              row == height - 1 || col == width - 1;
            const uint8_t colour = edge ? stroke : fill;
            if (colour == 0) continue;   // 0 is transparent, not palette 0
            setPixel(x + static_cast<int32_t>(col),
                     y + static_cast<int32_t>(row),
                     static_cast<uint8_t>(colour - 1));
        }
    }
}

void Console::hline(int32_t x, int32_t y, uint32_t length) {
    const uint8_t colour = drawColor(0);
    if (colour == 0) return;
    for (uint32_t i = 0; i < length; ++i) {
        setPixel(x + static_cast<int32_t>(i), y,
                 static_cast<uint8_t>(colour - 1));
    }
}

void Console::line(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    const uint8_t colour = drawColor(0);
    if (colour == 0) return;
    const uint8_t index = static_cast<uint8_t>(colour - 1);

    int32_t dx = std::abs(x2 - x1);
    int32_t dy = -std::abs(y2 - y1);
    int32_t sx = x1 < x2 ? 1 : -1;
    int32_t sy = y1 < y2 ? 1 : -1;
    int32_t error = dx + dy;

    while (true) {
        setPixel(x1, y1, index);
        if (x1 == x2 && y1 == y2) break;
        const int32_t doubled = error * 2;
        if (doubled >= dy) { error += dy; x1 += sx; }
        if (doubled <= dx) { error += dx; y1 += sy; }
    }
}

void Console::setGamepad(int player, uint8_t state) {
    if (player < 0 || player > 3) return;
    memory_[kGamepad1 + player] = state;
}

void Console::vline(int32_t x, int32_t y, uint32_t length) {
    const uint8_t colout = drawColor(0);
    if (colour == 0) return;
    for (uint32_t i = 0; i < length; ++i) {
        setPixel(x, y + static_cast<int32_t>(i),
                 static_cast<uint8_t>(colour - 1));
    }
}

void Console::oval(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    const uint8_t fill = drawColor(0);
    const uint8_t stroke = drawColor(1);
    if (width == 0 || height == 0) return;

    const int32_t a = static_cast<int32_t>(width) / 2;
    const int32_t b = static_cast<int32_t>(height) / 2;
    const int32_t cx = x + a;
    const int32_t cy = y + b;

    const auto plot = [&](int32_t px, int32_t py, uint8_t colour) {
        if (colour == 0) return;
        setPixel(px, py, static_cast<uint8_t>(colour - 1));
    };

    for (int32_t dy = -b; dy <= b; ++dy) {
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) * b * b + dy * dy * a * a <=
               a * a * b * b) {
            ++dx;
        }
        for (int32_t px = -dx; px <= dx; ++px) {
            const bool edge = px == -dx || px == dx || dy == -b || dy == b;
            plot(cx + px, cy + dy, edge ? stroke : fill);
        }
    }

}

void Console::blit(uint32_t sprite, int32_t x, int32_t y, uint32_t width,
                   uint32_t height, uint32_t flags) {
    blitSub(sprite, x, y, width, height, 0, 0, width, flags);
}

void Console::blitSub(uint32_t sprite, int32_t x, int32_t y, uint32_t width,
                      uint32_t height, uint32_t src_x, uint32_t src_y,
                      uint32_t stride, uint32_t flags) {
    const bool two_bpp = (flags & 0x01) != 0;
    const bool flip_x = (flags & 0x02) != 0;
    const bool flip_y = (flags & 0x04) != 0;
    const bool rotate = (flags & 0x08) != 0;

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            uint32_t sx = rotate ? row : col;
            uint32_t sy = rotate ? col : row;
            if (flip_x) sx = (rotate ? height : width) - 1 - sx;
            if (flip_y) sy = (rotate ? width : height) - 1 - sy;

            const uint32_t bit_index =
                (src_y + sy) * stride + (src_x + sx);

            uint8_t value = 0;
            if (two_bpp) {
                const uint32_t at = sprite + bit_index / 4;
                if (at >= kMemorySize) continue;
                value = static_cast<uint8_t>(
                    (memory_[at] >> ((3 - (bit_index % 4)) * 2)) & 0x03);
            } else {
                const uint32_t at = sprite + bit_index / 8;
                if (at >= kMemorySize) continue;
                value = static_cast<uint8_t>(
                    (memory_[at] >> (7 - (bit_index % 8))) & 0x01);
            }

            const uint8_t colour = drawColor(static_cast<int>(value));
            if (colour == 0) continue;   // nibble 0 means transparent
            setPixel(x + static_cast<int32_t>(col),
                     y + static_cast<int32_t>(row),
                     static_cast<uint8_t>(colour - 1));
        }
    }
}

void Console::text(const char* string, int32_t x, int32_t y) {
    if (!string) return;

    int32_t pen_x = x;
    int32_t pen_y = y;
    for (const char* c = string; *c; ++c) {
        if (*c == '\n') {
            pen_x = x;
            pen_y += 8;
            continue;
        }
        unsigned char glyph = static_cast<unsigned char>(*c);
        // The bundled table stops at 95, so lowercase is folded to
        // uppercase rather than rendered as a blank. A vendored
        // official font covers 32-255 and makes this a no-op.
        if (glyph >= 97 && glyph <= 122) glyph -= 32;
        if (glyph >= kFontFirst && glyph < kFontFirst + kFontCount) {
            drawGlyph(glyph - kFontFirst, pen_x, pen_y);
        }
        pen_x += 8;
    }
}

void Console::trace(const char* string) {
    if (string) std::fprintf(stderr, "[wasm4] %s\n", string);
}

void Console::drawGlyph(int glyph, int32_t x, int32_t y) {
    if (glyph < 0 || glyph >= static_cast<int>(kFontCount)) return;

    const uint8_t colour = drawColor(0);
    if (colour == 0) return;   // nibble 0 means transparent
    const uint8_t index = static_cast<uint8_t>(colour - 1);

    const uint8_t* rows = kFont + glyph * 8;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if ((rows[row] >> (7 - col)) & 1) {
                setPixel(x + col, y + row, index);
            }
        }
    }
}

bool Console::restore(const uint8_t* bytes, std::size_t size,
                      bool started) {
    if (!bytes || size != memory_.size()) return false;
    std::memcpy(memory_.data(), bytes, size);
    started_ = started;
    // The framebuffer is derived from memory rather than stored, so a
    // state loaded mid-frame would otherwise show the previous
    // picture for one frame.
    present();
    return true;
}

}