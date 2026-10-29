#include "ppu.h"
#include "cartridge.h"

#include <cstring>

namespace nes {

const uint32_t PPU::kPalette[64] = {
    0xFF545454, 0xFF001E74, 0xFF081090, 0xFF300088, 0xFF440064, 0xFF5C0030,
    0xFF540400, 0xFF3C1800, 0xFF202A00, 0xFF083A00, 0xFF004000, 0xFF003C00,
    0xFF00323C, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF989698, 0xFF084CC4, 0xFF3032EC, 0xFF5C1EE4, 0xFF8814B0, 0xFFA01464,
    0xFF982220, 0xFF783C00, 0xFF545A00, 0xFF287200, 0xFF087C00, 0xFF007628,
    0xFF006678, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFF4C9AEC, 0xFF787CEC, 0xFFB062EC, 0xFFE454EC, 0xFFEC58B4,
    0xFFEC6A64, 0xFFD48820, 0xFFA0AA00, 0xFF74C400, 0xFF4CD020, 0xFF38CC6C,
    0xFF38B4CC, 0xFF3C3C3C, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFFA8CCEC, 0xFFBCBCEC, 0xFFD4B2EC, 0xFFECAEEC, 0xFFECAED4,
    0xFFECB4B0, 0xFFE4C490, 0xFFCCD278, 0xFFB4DE78, 0xFFA8E290, 0xFF98E2B4,
    0xFFA0D6E4, 0xFFA0A2A0, 0xFF000000, 0xFF000000,
};

namespace {

int nametableIndex(uint16_t addr, Mirroring m) {
    const int quadrant = (addr >> 10) & 0x03;
    switch (m) {
    case Mirroring::Vertical:    return quadrant & 1;         
    case Mirroring::Horizontal:  return (quadrant >> 1) & 1; 
    case Mirroring::OneScreenLo: return 0;
    case Mirroring::OneScreenHi: return 1;
    default:                     return quadrant & 1;
    }
}

uint8_t reverseBits(uint8_t b) {
    b = static_cast<uint8_t>(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

}  // namespace

PPU::PPU() { reset(); }

void PPU::reset() {
    ctrl_ = mask_ = status_ = 0;
    fine_x_ = 0;
    write_toggle_ = 0;
    read_buffer_ = 0;
    vram_addr_.reg = 0;
    tram_addr_.reg = 0;
    scanline_ = 0;
    cycle_ = 0;
    odd_frame_ = false;
    nmi = false;
    frame_complete = false;

    bg_next_id_ = bg_next_attr_ = bg_next_lo_ = bg_next_hi_ = 0;
    bg_shift_pattern_lo_ = bg_shift_pattern_hi_ = 0;
    bg_shift_attr_lo_ = bg_shift_attr_hi_ = 0;
    sprite_count_ = 0;
    sprite_zero_possible_ = sprite_zero_rendered_ = false;

    std::memset(frame_, 0, sizeof(frame_));
}


uint8_t PPU::ppuRead(uint16_t addr) {
    addr &= 0x3FFF;
    uint8_t data = 0;

    if (cart_ && cart_->ppuRead(addr, data)) return data;

    if (addr <= 0x1FFF) {
        return 0; 
    }
    if (addr <= 0x3EFF) {
        const Mirroring m = cart_ ? cart_->mirroring() : Mirroring::Horizontal;
        const uint16_t a = addr & 0x0FFF;
        return nametable_[nametableIndex(a, m)][a & 0x03FF];
    }

    uint16_t p = addr & 0x001F;
    if (p == 0x10 || p == 0x14 || p == 0x18 || p == 0x1C) p &= 0x0F;
    return palette_[p] & (maskGrayscale() ? 0x30 : 0x3F);
}

void PPU::ppuWrite(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;

    if (cart_ && cart_->ppuWrite(addr, data)) return;

    if (addr <= 0x1FFF) {
        return;  
    }
    if (addr <= 0x3EFF) {
        const Mirroring m = cart_ ? cart_->mirroring() : Mirroring::Horizontal;
        const uint16_t a = addr & 0x0FFF;
        nametable_[nametableIndex(a, m)][a & 0x03FF] = data;
        return;
    }

    uint16_t p = addr & 0x001F;
    if (p == 0x10 || p == 0x14 || p == 0x18 || p == 0x1C) p &= 0x0F;
    palette_[p] = data;
}


uint8_t PPU::cpuRead(uint16_t addr, bool readonly) {
    uint8_t data = 0;

    if (readonly) {
        switch (addr & 0x07) {
        case 2: return static_cast<uint8_t>((status_ & 0xE0) | (read_buffer_ & 0x1F));
        case 4: return oamBytes()[oam_addr_];
        case 7: return read_buffer_;
        default: return 0;
        }
    }

    switch (addr & 0x07) {
    case 2:  
        data = static_cast<uint8_t>((status_ & 0xE0) | (read_buffer_ & 0x1F));
        status_ &= static_cast<uint8_t>(~0x80);
        write_toggle_ = 0;
        break;

    case 4:  
        data = oamBytes()[oam_addr_];
        break;

    case 7:  
        data = read_buffer_;
        read_buffer_ = ppuRead(vram_addr_.reg);
        if (vram_addr_.reg >= 0x3F00) data = read_buffer_;
        vram_addr_.reg = static_cast<uint16_t>(vram_addr_.reg + (ctrlIncrement32() ? 32 : 1));
        break;

    default:
        break;  
    }
    return data;
}

void PPU::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr & 0x07) {
    case 0:  
        ctrl_ = data;
        tram_addr_.setNtX(data & 0x01);
        tram_addr_.setNtY((data >> 1) & 0x01);
        break;

    case 1:  
        mask_ = data;
        break;

    case 3:  
        oam_addr_ = data;
        break;

    case 4:  
        oamBytes()[oam_addr_] = data;
        oam_addr_++;
        break;

    case 5:  
        if (write_toggle_ == 0) {
            fine_x_ = data & 0x07;
            tram_addr_.setCoarseX(data >> 3);
            write_toggle_ = 1;
        } else {
            tram_addr_.setFineY(data & 0x07);
            tram_addr_.setCoarseY(data >> 3);
            write_toggle_ = 0;
        }
        break;

    case 6:  
        if (write_toggle_ == 0) {
            tram_addr_.reg = static_cast<uint16_t>(
                (tram_addr_.reg & 0x00FF) | ((data & 0x3F) << 8));
            write_toggle_ = 1;
        } else {
            tram_addr_.reg = static_cast<uint16_t>((tram_addr_.reg & 0xFF00) | data);
            vram_addr_ = tram_addr_; 
            write_toggle_ = 0;
        }
        break;

    case 7:  
        ppuWrite(vram_addr_.reg, data);
        vram_addr_.reg = static_cast<uint16_t>(vram_addr_.reg + (ctrlIncrement32() ? 32 : 1));
        break;

    default:
        break;
    }
}


void PPU::incrementScrollX() {
    if (!rendering()) return;
    if (vram_addr_.coarseX() == 31) {
        vram_addr_.setCoarseX(0);
        vram_addr_.setNtX(~vram_addr_.ntX());  
    } else {
        vram_addr_.setCoarseX(vram_addr_.coarseX() + 1);
    }
}

void PPU::incrementScrollY() {
    if (!rendering()) return;

    if (vram_addr_.fineY() < 7) {
        vram_addr_.setFineY(vram_addr_.fineY() + 1);
        return;
    }
    vram_addr_.setFineY(0);

    if (vram_addr_.coarseY() == 29) {
        vram_addr_.setCoarseY(0);
        vram_addr_.setNtY(~vram_addr_.ntY());
    } else if (vram_addr_.coarseY() == 31) {
        vram_addr_.setCoarseY(0);
    } else {
        vram_addr_.setCoarseY(vram_addr_.coarseY() + 1);
    }
}

void PPU::transferAddressX() {
    if (!rendering()) return;
    vram_addr_.setNtX(tram_addr_.ntX());
    vram_addr_.setCoarseX(tram_addr_.coarseX());
}

void PPU::transferAddressY() {
    if (!rendering()) return;
    vram_addr_.setFineY(tram_addr_.fineY());
    vram_addr_.setNtY(tram_addr_.ntY());
    vram_addr_.setCoarseY(tram_addr_.coarseY());
}

void PPU::loadBackgroundShifters() {
    bg_shift_pattern_lo_ = static_cast<uint16_t>((bg_shift_pattern_lo_ & 0xFF00) | bg_next_lo_);
    bg_shift_pattern_hi_ = static_cast<uint16_t>((bg_shift_pattern_hi_ & 0xFF00) | bg_next_hi_);

    bg_shift_attr_lo_ = static_cast<uint16_t>((bg_shift_attr_lo_ & 0xFF00) |
                                              ((bg_next_attr_ & 0x01) ? 0xFF : 0x00));
    bg_shift_attr_hi_ = static_cast<uint16_t>((bg_shift_attr_hi_ & 0xFF00) |
                                              ((bg_next_attr_ & 0x02) ? 0xFF : 0x00));
}

void PPU::updateShifters() {
    if (maskShowBg()) {
        bg_shift_pattern_lo_ = static_cast<uint16_t>(bg_shift_pattern_lo_ << 1);
        bg_shift_pattern_hi_ = static_cast<uint16_t>(bg_shift_pattern_hi_ << 1);
        bg_shift_attr_lo_    = static_cast<uint16_t>(bg_shift_attr_lo_ << 1);
        bg_shift_attr_hi_    = static_cast<uint16_t>(bg_shift_attr_hi_ << 1);
    }

    if (maskShowSpr() && cycle_ >= 1 && cycle_ < 258) {
        for (int i = 0; i < sprite_count_; ++i) {
            if (scanline_sprites_[i].x > 0) {
                scanline_sprites_[i].x--;  // count down to the sprite's column
            } else {
                sprite_shift_lo_[i] = static_cast<uint8_t>(sprite_shift_lo_[i] << 1);
                sprite_shift_hi_[i] = static_cast<uint8_t>(sprite_shift_hi_[i] << 1);
            }
        }
    }
}


void PPU::evaluateSprites() {
    std::memset(scanline_sprites_, 0xFF, sizeof(scanline_sprites_));
    sprite_count_ = 0;
    for (int i = 0; i < 8; ++i) {
        sprite_shift_lo_[i] = 0;
        sprite_shift_hi_[i] = 0;
    }
    sprite_zero_possible_ = false;

    const int height = ctrlTallSprites() ? 16 : 8;

    for (int n = 0; n < 64 && sprite_count_ < 9; ++n) {
        const int diff = scanline_ - static_cast<int>(oam_[n].y);
        if (diff < 0 || diff >= height) continue;

        if (sprite_count_ < 8) {
            if (n == 0) sprite_zero_possible_ = true; 
            scanline_sprites_[sprite_count_] = oam_[n];
            sprite_count_++;
        } else {
            status_ |= 0x20;  
            break;
        }
    }
}

void PPU::loadSpritePatterns() {
    for (int i = 0; i < sprite_count_; ++i) {
        const Sprite& s = scanline_sprites_[i];
        const bool flip_v = (s.attribute & 0x80) != 0;
        const bool flip_h = (s.attribute & 0x40) != 0;
        const int  row    = scanline_ - static_cast<int>(s.y);

        uint16_t addr;
        if (!ctrlTallSprites()) {
            const int r = flip_v ? (7 - row) : row;
            addr = static_cast<uint16_t>((ctrlSpritePat() << 12) | (s.tile << 4) | (r & 0x07));
        } else {
            const int  r        = flip_v ? (15 - row) : row;
            const bool bottom   = r >= 8;
            const uint16_t base = static_cast<uint16_t>((s.tile & 0x01) << 12);
            const uint16_t tile = static_cast<uint16_t>((s.tile & 0xFE) + (bottom ? 1 : 0));
            addr = static_cast<uint16_t>(base | (tile << 4) | (r & 0x07));
        }

        uint8_t lo = ppuRead(addr);
        uint8_t hi = ppuRead(static_cast<uint16_t>(addr + 8));

        if (flip_h) {
            lo = reverseBits(lo);
            hi = reverseBits(hi);
        }

        sprite_shift_lo_[i] = lo;
        sprite_shift_hi_[i] = hi;
    }
}


void PPU::clock() {
    if (scanline_ >= -1 && scanline_ < 240) {

        if (scanline_ == 0 && cycle_ == 0 && odd_frame_ && rendering()) {
            cycle_ = 1;
        }

        if (scanline_ == -1 && cycle_ == 1) {
            status_ &= static_cast<uint8_t>(~0xE0); 
            sprite_zero_rendered_ = false;
            for (int i = 0; i < 8; ++i) {
                sprite_shift_lo_[i] = 0;
                sprite_shift_hi_[i] = 0;
            }
        }

        if ((cycle_ >= 2 && cycle_ < 258) || (cycle_ >= 321 && cycle_ < 338)) {
            updateShifters();

            switch ((cycle_ - 1) % 8) {
            case 0:
                loadBackgroundShifters();
                bg_next_id_ = ppuRead(static_cast<uint16_t>(0x2000 | (vram_addr_.reg & 0x0FFF)));
                break;

            case 2: {
                const uint16_t a = static_cast<uint16_t>(
                    0x23C0 | (vram_addr_.ntY() << 11) | (vram_addr_.ntX() << 10) |
                    ((vram_addr_.coarseY() >> 2) << 3) | (vram_addr_.coarseX() >> 2));
                bg_next_attr_ = ppuRead(a);
                if (vram_addr_.coarseY() & 0x02) bg_next_attr_ >>= 4;
                if (vram_addr_.coarseX() & 0x02) bg_next_attr_ >>= 2;
                bg_next_attr_ &= 0x03;
                break;
            }

            case 4:
                bg_next_lo_ = ppuRead(static_cast<uint16_t>(
                    (ctrlBgPat() << 12) + (bg_next_id_ << 4) + vram_addr_.fineY()));
                break;

            case 6:
                // The two bitplanes of a tile sit eight bytes apart.
                bg_next_hi_ = ppuRead(static_cast<uint16_t>(
                    (ctrlBgPat() << 12) + (bg_next_id_ << 4) + vram_addr_.fineY() + 8));
                break;

            case 7:
                incrementScrollX();
                break;

            default:
                break;
            }
        }

        if (cycle_ == 256) incrementScrollY();

        if (cycle_ == 257) {
            loadBackgroundShifters();
            transferAddressX();
            if (scanline_ >= 0) evaluateSprites();
        }

        if (scanline_ == -1 && cycle_ >= 280 && cycle_ < 305) transferAddressY();

        if (cycle_ == 340) loadSpritePatterns();

        if (cycle_ == 338 || cycle_ == 340) {
            bg_next_id_ = ppuRead(static_cast<uint16_t>(0x2000 | (vram_addr_.reg & 0x0FFF)));
        }
    }

    if (scanline_ == 241 && cycle_ == 1) {
        status_ |= 0x80;                  
        if (ctrlNmiEnabled()) nmi = true;
    }

    uint8_t bg_pixel = 0, bg_palette = 0;
    if (maskShowBg() && (maskBgLeft() || cycle_ >= 9)) {
        const uint16_t bit = static_cast<uint16_t>(0x8000 >> fine_x_);
        const uint8_t p0 = (bg_shift_pattern_lo_ & bit) ? 1 : 0;
        const uint8_t p1 = (bg_shift_pattern_hi_ & bit) ? 1 : 0;
        bg_pixel = static_cast<uint8_t>((p1 << 1) | p0);

        const uint8_t a0 = (bg_shift_attr_lo_ & bit) ? 1 : 0;
        const uint8_t a1 = (bg_shift_attr_hi_ & bit) ? 1 : 0;
        bg_palette = static_cast<uint8_t>((a1 << 1) | a0);
    }

    uint8_t fg_pixel = 0, fg_palette = 0;
    bool fg_behind_bg = false;
    if (maskShowSpr() && (maskSprLeft() || cycle_ >= 9)) {
        sprite_zero_rendered_ = false;
        for (int i = 0; i < sprite_count_; ++i) {
            if (scanline_sprites_[i].x != 0) continue;  

            const uint8_t p0 = (sprite_shift_lo_[i] & 0x80) ? 1 : 0;
            const uint8_t p1 = (sprite_shift_hi_[i] & 0x80) ? 1 : 0;
            const uint8_t px = static_cast<uint8_t>((p1 << 1) | p0);
            if (px == 0) continue;  

            fg_pixel     = px;
            fg_palette   = static_cast<uint8_t>((scanline_sprites_[i].attribute & 0x03) + 4);
            fg_behind_bg = (scanline_sprites_[i].attribute & 0x20) != 0;
            if (i == 0) sprite_zero_rendered_ = true;
            break;
        }
    }

    uint8_t pixel = 0, palette = 0;
    if (bg_pixel == 0 && fg_pixel > 0) {
        pixel = fg_pixel;  palette = fg_palette;
    } else if (bg_pixel > 0 && fg_pixel == 0) {
        pixel = bg_pixel;  palette = bg_palette;
    } else if (bg_pixel > 0 && fg_pixel > 0) {
        if (fg_behind_bg) { pixel = bg_pixel; palette = bg_palette; }
        else              { pixel = fg_pixel; palette = fg_palette; }

        if (sprite_zero_possible_ && sprite_zero_rendered_ &&
            maskShowBg() && maskShowSpr()) {
            const int first = (maskBgLeft() || maskSprLeft()) ? 1 : 9;
            if (cycle_ >= first && cycle_ < 258) status_ |= 0x40;
        }
    }

    if (scanline_ >= 0 && scanline_ < kScreenHeight &&
        cycle_ >= 1 && cycle_ <= kScreenWidth) {
        const uint8_t index = ppuRead(static_cast<uint16_t>(0x3F00 + (palette << 2) + pixel));
        frame_[scanline_ * kScreenWidth + (cycle_ - 1)] = kPalette[index & 0x3F];
    }

    // ------------------------------------------------------------ advance
    cycle_++;
    if (cycle_ >= 341) {
        cycle_ = 0;
        scanline_++;
        if (scanline_ >= 261) {
            scanline_ = -1;
            frame_complete = true;
            odd_frame_ = !odd_frame_;
        }
    }
}

}  // namespace nes