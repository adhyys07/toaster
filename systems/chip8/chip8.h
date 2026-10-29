#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::chip8 {

constexpr int kDisplayWidth = 64;
constexpr int kDisplayHeight = 32;

class Chip8 {
public:
    bool loadRom(const std::string& path, std::string& error);
    void reset();
    bool step();
    void tick60Hz();
    void setKey(uint8_t key, bool pressed);
    const std::array<uint8_t, kDisplayWidth * kDisplayHeight>& display() const { return display_; }
    void clearDrawFlag() { draw_flag_ = false; }
    bool drawFlag() const { return draw_flag_; }
    bool soundActive() const { return sound_timer_ != 0; }

private:
    bool execute(uint16_t opcode);
    uint8_t randomByte();

    std::vector<uint8_t> rom_;
    std::array<uint8_t, 4096> memory_{};
    std::array<uint8_t, 16> v_{};
    std::array<uint16_t, 16> stack_{};
    std::array<uint8_t, 16> keys_{};
    std::array<uint8_t, kDisplayWidth * kDisplayHeight> display_{};
    uint16_t pc_ = 0x200;
    uint16_t index_ = 0;
    uint8_t sp_ = 0;
    uint8_t delay_timer_ = 0;
    uint8_t sound_timer_ = 0;
    bool draw_flag_ = false;
    uint32_t random_state_ = 0xC8;
};

}  // namespace emu::chip8
