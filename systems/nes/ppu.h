#pragma once

#include <cstdint>

namespace nes {

class Cartridge;

constexpr int kScreenWidth  = 256;
constexpr int kScreenHeight = 240;

class PPU {
public:
    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit.block(nametable_, sizeof(nametable_));
        visit.block(palette_, sizeof(palette_));
        visit.block(oam_, sizeof(oam_));
        visit(oam_addr_);
        visit.block(scanline_sprites_, sizeof(scanline_sprites_));
        visit(sprite_count_);
        visit.block(sprite_shift_lo_, sizeof(sprite_shift_lo_));
        visit.block(sprite_shift_hi_, sizeof(sprite_shift_hi_));
        visit(sprite_zero_possible_); visit(sprite_zero_rendered_);
        visit(ctrl_); visit(mask_); visit(status_);
        visit(vram_addr_.reg); visit(tram_addr_.reg);
        visit(fine_x_); visit(write_toggle_); visit(read_buffer_);
        visit(bg_next_id_); visit(bg_next_attr_);
        visit(bg_next_lo_); visit(bg_next_hi_);
        visit(bg_shift_pattern_lo_); visit(bg_shift_pattern_hi_);
        visit(bg_shift_attr_lo_); visit(bg_shift_attr_hi_);
        visit(scanline_); visit(cycle_); visit(odd_frame_);
        visit(nmi); visit(frame_complete);
        visit.block(frame_, sizeof(frame_));
    }

    PPU();

    void connectCartridge(Cartridge* cart) { cart_ = cart; }
    void reset();
    void clock();  

    uint8_t cpuRead(uint16_t addr, bool readonly = false);
    void    cpuWrite(uint16_t addr, uint8_t data);

    uint8_t ppuRead(uint16_t addr);
    void    ppuWrite(uint16_t addr, uint8_t data);

    const uint32_t* framebuffer() const { return frame_; }

    // Replaces the 64-entry NTSC colour table. Entries are 0xAARRGGBB.
    void setPalette(const uint32_t* entries);
    uint8_t* oamBytes() { return reinterpret_cast<uint8_t*>(oam_); }

    bool nmi            = false;  
    bool frame_complete = false;

    int scanline() const { return scanline_; }
    int dot()      const { return cycle_; }

private:
    struct Loopy {
        uint16_t reg = 0;

        uint16_t coarseX() const { return reg & 0x001F; }
        uint16_t coarseY() const { return (reg >> 5) & 0x001F; }
        uint16_t ntX()     const { return (reg >> 10) & 1; }
        uint16_t ntY()     const { return (reg >> 11) & 1; }
        uint16_t fineY()   const { return (reg >> 12) & 0x0007; }

        void setCoarseX(uint16_t v) { reg = static_cast<uint16_t>((reg & ~0x001F) | (v & 0x1F)); }
        void setCoarseY(uint16_t v) { reg = static_cast<uint16_t>((reg & ~0x03E0) | ((v & 0x1F) << 5)); }
        void setNtX(uint16_t v)     { reg = static_cast<uint16_t>((reg & ~0x0400) | ((v & 1) << 10)); }
        void setNtY(uint16_t v)     { reg = static_cast<uint16_t>((reg & ~0x0800) | ((v & 1) << 11)); }
        void setFineY(uint16_t v)   { reg = static_cast<uint16_t>((reg & ~0x7000) | ((v & 7) << 12)); }
    };

    struct Sprite {
        uint8_t y, tile, attribute, x;
    };

    void incrementScrollX();
    void incrementScrollY();
    void transferAddressX();
    void transferAddressY();
    void loadBackgroundShifters();
    void updateShifters();
    void evaluateSprites();
    void loadSpritePatterns();

    Cartridge* cart_ = nullptr;

    uint8_t nametable_[2][1024] = {};
    uint8_t palette_[32]        = {};

    Sprite  oam_[64]     = {};
    uint8_t oam_addr_    = 0;
    Sprite  scanline_sprites_[8] = {};
    uint8_t sprite_count_        = 0;
    uint8_t sprite_shift_lo_[8]  = {};
    uint8_t sprite_shift_hi_[8]  = {};
    bool    sprite_zero_possible_ = false;
    bool    sprite_zero_rendered_ = false;

    uint8_t ctrl_ = 0;
    bool ctrlIncrement32()   const { return (ctrl_ & 0x04) != 0; }
    uint16_t ctrlSpritePat() const { return (ctrl_ & 0x08) ? 1 : 0; }
    uint16_t ctrlBgPat()     const { return (ctrl_ & 0x10) ? 1 : 0; }
    bool ctrlTallSprites()   const { return (ctrl_ & 0x20) != 0; }
    bool ctrlNmiEnabled()    const { return (ctrl_ & 0x80) != 0; }

    uint8_t mask_ = 0;
    bool maskGrayscale()  const { return (mask_ & 0x01) != 0; }
    bool maskBgLeft()     const { return (mask_ & 0x02) != 0; }
    bool maskSprLeft()    const { return (mask_ & 0x04) != 0; }
    bool maskShowBg()     const { return (mask_ & 0x08) != 0; }
    bool maskShowSpr()    const { return (mask_ & 0x10) != 0; }
    bool rendering()      const { return (mask_ & 0x18) != 0; }

    uint8_t status_ = 0;

    Loopy    vram_addr_;
    Loopy    tram_addr_;
    uint8_t  fine_x_       = 0;
    uint8_t  write_toggle_ = 0;  
    uint8_t  read_buffer_  = 0;

    uint8_t  bg_next_id_   = 0;
    uint8_t  bg_next_attr_ = 0;
    uint8_t  bg_next_lo_   = 0;
    uint8_t  bg_next_hi_   = 0;
    uint16_t bg_shift_pattern_lo_ = 0;
    uint16_t bg_shift_pattern_hi_ = 0;
    uint16_t bg_shift_attr_lo_    = 0;
    uint16_t bg_shift_attr_hi_    = 0;

    int  scanline_ = 0;
    int  cycle_    = 0;
    bool odd_frame_ = false;

    uint32_t frame_[kScreenWidth * kScreenHeight] = {};

    // The live scheme, seeded from kDefaultPalette by the constructor. It is
    // deliberately left out of reset() and visitState(): resetting the game
    // or loading an old save state must not drag a previous colour scheme
    // back with it.
    uint32_t palette_rgb_[64] = {};

    static const uint32_t kDefaultPalette[64];
};

}  // namespace nes