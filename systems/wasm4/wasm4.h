#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu::wasm4 {

constexpr int kScreenWidth = 160;
constexpr int kScreenHeight = 160;

constexpr uint32_t kPalette     = 0x0004;   
constexpr uint32_t kDrawColors  = 0x0014;   
constexpr uint32_t kGamepad1    = 0x0016;   
constexpr uint32_t kMouseX      = 0x001A;
constexpr uint32_t kSystemFlags = 0x001F;
constexpr uint32_t kFramebuffer = 0x00A0;
constexpr uint32_t kMemorySize  = 0x10000;

class Runtime {

public:
    virtual ~Runtime() = default;
    virtual bool load(const std::vector<uint8_t>& cartridge, uint8_t* memory,
                      std::string& error) = 0;

    virtual void callStart() = 0;
    virtual void callUpdate() = 0;

    virtual bool ready() const = 0;
};

std::unique_ptr<Runtime> makeWasm3Runtime();

class Console {
public:
    bool loadRom(const std::string& path, std::string& error);
    void reset();
    void runFrame();

    bool started() const { return started_; }
    bool restore(const uint8_t* bytes, std::size_t size, bool started);


    const uint32_t* framebuffer() const { return framebuffer_.data(); }
    void setGamepad(int player, uint8_t state);

    uint8_t peek(uint32_t address) const {
        return address < kMemorySize ? memory_[address] : uint8_t{0};
    }

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit.block(memory_.data(), memory_.size());
        visit(started_);
    }

    void blit(uint32_t sprite, int32_t x, int32_t y, uint32_t width,
              uint32_t height, uint32_t flags);
    void blitSub(uint32_t sprite, int32_t x, int32_t y, uint32_t width,
                 uint32_t height, uint32_t src_x, uint32_t src_y,
                 uint32_t stride, uint32_t flags);
    void line(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
    void hline(int32_t x, int32_t y, uint32_t length);
    void vline(int32_t x, int32_t y, uint32_t length);
    void oval(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void rect(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void text(const char* string, int32_t x, int32_t y);
    void trace(const char* string);

private:
    void present();
    void setPixel(int32_t x, int32_t y, uint8_t colour);
    void drawGlyph(int glyph, int32_t x, int32_t y);

    uint8_t drawColor(int index) const;

    std::unique_ptr<Runtime> runtime_;
    std::vector<uint8_t> cartridge_;
    std::array<uint8_t, kMemorySize> memory_{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    bool started_ = false;

};

}