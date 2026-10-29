#include "chip8.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace emu::chip8 {
namespace {
constexpr uint16_t kProgramStart = 0x200;
constexpr uint8_t kFont[] = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
    0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
    0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80
};
}

bool Chip8::loadRom(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    if (data.empty()) {
        error = "CHIP-8 ROM is empty";
        return false;
    }
    if (data.size() > memory_.size() - kProgramStart) {
        error = "CHIP-8 ROM is too large (maximum 3584 bytes)";
        return false;
    }
    rom_ = std::move(data);
    reset();
    error.clear();
    return true;
}

void Chip8::reset() {
    memory_.fill(0);
    std::copy(std::begin(kFont), std::end(kFont), memory_.begin());
    if (!rom_.empty()) std::copy(rom_.begin(), rom_.end(), memory_.begin() + kProgramStart);
    v_.fill(0);
    stack_.fill(0);
    keys_.fill(0);
    display_.fill(0);
    pc_ = kProgramStart;
    index_ = 0;
    sp_ = 0;
    delay_timer_ = sound_timer_ = 0;
    draw_flag_ = false;
    random_state_ = 0xC8;
}

void Chip8::setKey(uint8_t key, bool pressed) {
    if (key < keys_.size()) keys_[key] = pressed ? 1 : 0;
}

void Chip8::tick60Hz() {
    if (delay_timer_) --delay_timer_;
    if (sound_timer_) --sound_timer_;
}

uint8_t Chip8::randomByte() {
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return static_cast<uint8_t>(random_state_ >> 16);
}

bool Chip8::step() {
    if (pc_ >= memory_.size() - 1) return false;
    const uint16_t opcode = static_cast<uint16_t>((memory_[pc_] << 8) | memory_[pc_ + 1]);
    pc_ += 2;
    return execute(opcode);
}

bool Chip8::execute(uint16_t op) {
    const uint8_t x = (op >> 8) & 0x0F;
    const uint8_t y = (op >> 4) & 0x0F;
    const uint8_t n = op & 0x0F;
    const uint8_t kk = op & 0xFF;
    const uint16_t nnn = op & 0x0FFF;
    switch (op & 0xF000) {
    case 0x0000:
        if (op == 0x00E0) display_.fill(0);
        else if (op == 0x00EE) { if (!sp_) return false; pc_ = stack_[--sp_]; }
        else return false;
        draw_flag_ = true; return true;
    case 0x1000: pc_ = nnn; return true;
    case 0x2000: if (sp_ >= stack_.size()) return false; stack_[sp_++] = pc_; pc_ = nnn; return true;
    case 0x3000: if (v_[x] == kk) pc_ += 2; return true;
    case 0x4000: if (v_[x] != kk) pc_ += 2; return true;
    case 0x5000: if (n || (op & 0x000F)) return false; if (v_[x] == v_[y]) pc_ += 2; return true;
    case 0x6000: v_[x] = kk; return true;
    case 0x7000: v_[x] = static_cast<uint8_t>(v_[x] + kk); return true;
    case 0x8000:
        switch (n) {
        case 0x0: v_[x] = v_[y]; break;
        case 0x1: v_[x] |= v_[y]; break;
        case 0x2: v_[x] &= v_[y]; break;
        case 0x3: v_[x] ^= v_[y]; break;
        case 0x4: { const uint16_t sum = v_[x] + v_[y]; v_[0xF] = sum > 0xFF; v_[x] = sum & 0xFF; break; }
        case 0x5: v_[0xF] = v_[x] >= v_[y]; v_[x] = static_cast<uint8_t>(v_[x] - v_[y]); break;
        case 0x6: v_[0xF] = v_[x] & 1; v_[x] >>= 1; break;
        case 0x7: v_[0xF] = v_[y] >= v_[x]; v_[x] = static_cast<uint8_t>(v_[y] - v_[x]); break;
        case 0xE: v_[0xF] = (v_[x] >> 7) & 1; v_[x] <<= 1; break;
        default: return false;
        } return true;
    case 0x9000: if (n) return false; if (v_[x] != v_[y]) pc_ += 2; return true;
    case 0xA000: index_ = nnn; return true;
    case 0xB000: pc_ = static_cast<uint16_t>(nnn + v_[0]); return true;
    case 0xC000: v_[x] = randomByte() & kk; return true;
    case 0xD000: {
        const uint8_t height = n;
        v_[0xF] = 0;
        for (uint8_t row = 0; row < height; ++row) {
            if (index_ + row >= memory_.size()) return false;
            const uint8_t sprite = memory_[index_ + row];
            for (uint8_t col = 0; col < 8; ++col) {
                if ((sprite & (0x80 >> col)) == 0) continue;
                const size_t px = (v_[x] + col) % kDisplayWidth;
                const size_t py = (v_[y] + row) % kDisplayHeight;
                uint8_t& pixel = display_[py * kDisplayWidth + px];
                if (pixel) v_[0xF] = 1;
                pixel ^= 1;
            }
        }
        draw_flag_ = true; return true;
    }
    case 0xE000:
        if (kk == 0x9E) { if (v_[x] < 16 && keys_[v_[x]]) pc_ += 2; return true; }
        if (kk == 0xA1) { if (v_[x] >= 16 || !keys_[v_[x]]) pc_ += 2; return true; }
        return false;
    case 0xF000:
        switch (kk) {
        case 0x07: v_[x] = delay_timer_; return true;
        case 0x0A:
            for (uint8_t key = 0; key < 16; ++key) if (keys_[key]) { v_[x] = key; return true; }
            pc_ -= 2; return true;
        case 0x15: delay_timer_ = v_[x]; return true;
        case 0x18: sound_timer_ = v_[x]; return true;
        case 0x1E: index_ = static_cast<uint16_t>(index_ + v_[x]); return true;
        case 0x29: index_ = static_cast<uint16_t>((v_[x] & 0x0F) * 5); return true;
        case 0x33:
            if (index_ > memory_.size() - 3) return false;
            memory_[index_] = v_[x] / 100; memory_[index_ + 1] = (v_[x] / 10) % 10; memory_[index_ + 2] = v_[x] % 10; return true;
        case 0x55:
            if (index_ + x >= memory_.size()) return false;
            for (uint8_t i = 0; i <= x; ++i) {
                memory_[index_ + i] = v_[i];
            }
            return true;
        case 0x65:
            if (index_ + x >= memory_.size()) return false;
            for (uint8_t i = 0; i <= x; ++i) {
                v_[i] = memory_[index_ + i];
            }
            return true;
        default: return false;
        }
    default: return false;
    }
}

}  // namespace emu::chip8
