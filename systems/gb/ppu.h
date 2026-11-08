#pragma once

#include <array>
#include <cstdint>

namespace emu::gb {

class Memory;

class PPU {
public:
    explicit PPU(Memory& memory);

    void reset();
    void tick(unsigned cycles);

    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    // Set when the controller enters vblank; the caller clears it to detect
    // the next frame.
    bool frameComplete() const { return frame_complete_; }
    void clearFrameComplete() { frame_complete_ = false; }

    uint8_t line() const { return ly_; }
    uint8_t mode() const { return mode_; }

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(lcdc_);
        visit(stat_);
        visit(scy_);
        visit(scx_);
        visit(ly_);
        visit(lyc_);
        visit(dma_);
        visit(bgp_);
        visit(obp0_);
        visit(obp1_);
        visit(wy_);
        visit(wx_);
        visit(dot_);
        visit(mode_);
        visit(window_line_);
        visit(stat_line_);
        visit(frame_complete_);
        visit.block(framebuffer_.data(),
                    framebuffer_.size() * sizeof(uint32_t));
        visit.block(line_pixels_.data(), line_pixels_.size());
        visit.block(line_priority_.data(), line_priority_.size());
    }

    void setColors(const uint32_t* colors);
    void setColorMode(bool on) { color_ = on; }

private:
    void step();
    void setMode(uint8_t mode);
    void updateStatLine();
    void renderScanLine();
    void renderBackground();
    void renderWindow();
    void renderSprites();
    void startOamDma(uint8_t page);
    uint8_t tilePixel(uint16_t data_base, uint8_t tile, int x, int y,
                      bool signed_index) const;
    uint8_t tilePixelBanked(uint16_t data_base, uint8_t tile, int x,
                            int y, bool signed_index, int bank) const;

    // BGR555 as the hardware stores it, to the ARGB the frontend wants.
    static uint32_t fromBgr555(uint8_t low, uint8_t high);

    // DMG and CGB disagree about sprite priority, so the rule lives in
    // one place rather than being inlined into renderSprites twice.
    bool backgroundWins(int x, bool sprite_behind) const;

    Memory& memory_;

    std::array<uint32_t, 160 * 144> framebuffer_{};
    // The background/window colour index per pixel of the current line, kept
    // so sprite priority can tell colour 0 from the rest.
    std::array<uint8_t, 160> line_pixels_{};
    // CGB gives the background an "above sprites" bit per tile, so
    // sprite priority needs more than the colour-zero test DMG used.
    std::array<uint8_t, 160> line_priority_{};

    uint8_t lcdc_ = 0x91;
    uint8_t stat_ = 0x85;
    uint8_t scy_ = 0;
    uint8_t scx_ = 0;
    uint8_t ly_ = 0;
    uint8_t lyc_ = 0;
    uint8_t dma_ = 0xFF;
    uint8_t bgp_ = 0xFC;
    uint8_t obp0_ = 0xFF;
    uint8_t obp1_ = 0xFF;
    uint8_t wy_ = 0;
    uint8_t wx_ = 0;
    // From the cartridge, so it stays out of visitState.
    bool color_ = false;

    uint32_t colors_[4] = {
        0xFFE0F8D0u, 0xFF88C070u, 0xFF346856u, 0xFF081820u
    };


    uint16_t dot_ = 0;
    uint8_t mode_ = 2;
    // The window has its own counter that only advances on lines it draws,
    // which is what lets a game scroll it independently of LY.
    uint8_t window_line_ = 0;
    bool stat_line_ = false;
    bool frame_complete_ = false;
};

} // namespace emu::gb
