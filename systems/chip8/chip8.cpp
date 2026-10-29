#include "../../src/core/system.h"
#include "chip8.h"

#include <cmath>
#include <cstring>

namespace emu {
namespace {

constexpr int    kSampleRate = 44100;
constexpr double kRefreshHz  = 60.0;
constexpr double kBeepHz     = 440.0;

class Chip8System final : public System {
public:
    Chip8System() {
        static const char* kKeys[16] = {
            "X", "1", "2", "3", "Q", "W", "E", "A",
            "S", "D", "Z", "C", "4", "R", "F", "V",
        };
        static const char* kNames[16] = {
            "Key 0", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6", "Key 7",
            "Key 8", "Key 9", "Key A", "Key B", "Key C", "Key D", "Key E", "Key F",
        };
        for (int i = 0; i < 16; ++i) {
            buttons_.push_back({ kNames[i], 1u << i, kKeys[i] });
        }
        frame_.resize(chip8::kDisplayWidth * chip8::kDisplayHeight, 0);
    }

    const char* name() const override { return "CHIP-8"; }

    bool loadRom(const std::string& path, std::string& error) override {
        return vm_.loadRom(path, error);
    }

    void reset() override { vm_.reset(); }

    void runFrame() override {
        for (int i = 0; i < cycles_per_frame_; ++i) {
            if (!vm_.step()) break;  // bad opcode: stop rather than spin
        }
        vm_.tick60Hz();

        const auto& display = vm_.display();
        for (size_t i = 0; i < display.size(); ++i) {
            frame_[i] = display[i] ? kOn : kOff;
        }
        vm_.clearDrawFlag();

        const size_t needed = kSampleRate / 60;
        audio_.resize(needed);
        const bool sounding = vm_.soundActive();
        for (size_t i = 0; i < needed; ++i) {
            if (!sounding) {
                audio_[i] = 0.0f;
                continue;
            }
            audio_[i] = (phase_ < 0.5) ? 0.06f : -0.06f;
            phase_ += kBeepHz / kSampleRate;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }

    VideoFrame video() const override {
        return { frame_.data(), chip8::kDisplayWidth, chip8::kDisplayHeight };
    }

    double refreshRate() const override { return kRefreshHz; }
    double aspectRatio() const override { return 2.0; }  // 64x32 square pixels
    int    sampleRate()  const override { return kSampleRate; }

    size_t readAudio(float* out, size_t frames) override {
        const size_t n = audio_.size() < frames ? audio_.size() : frames;
        std::memcpy(out, audio_.data(), n * sizeof(float));
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<long>(n));
        return n;
    }

    const std::vector<ButtonInfo>& buttons() const override { return buttons_; }

    void setInput(int port, uint32_t state) override {
        if (port != 0) return;
        for (uint8_t k = 0; k < 16; ++k) {
            vm_.setKey(k, (state & (1u << k)) != 0);
        }
    }

private:
    static constexpr uint32_t kOff = 0xFF11161Cu;
    static constexpr uint32_t kOn  = 0xFFE9D8A6u;

    chip8::Chip8 vm_;
    std::vector<uint32_t>   frame_;
    std::vector<float>      audio_;
    std::vector<ButtonInfo> buttons_;
    int    cycles_per_frame_ = 11;
    double phase_ = 0.0;
};

}  // namespace

SystemPtr createChip8System() { return std::make_unique<Chip8System>(); }

}  // namespace emu
