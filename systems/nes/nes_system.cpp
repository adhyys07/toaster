#include "../../src/core/system.h"
#include "bus.h"
#include "cartridge.h"

#include <cstring>

namespace emu {
namespace{
constexpr double kCpuHz      = 1789773.0;  
constexpr int    kSampleRate = 44100;
constexpr double kCyclesPerSample = kCpuHz / kSampleRate;

class NesSystem final : public System {
public:
    NesSystem() {
        buttons_ = {
            { "A",      nes::BTN_A,      "Z"      },
            { "B",      nes::BTN_B,      "X"      },
            { "Select", nes::BTN_SELECT, "Right Shift" },
            { "Start",  nes::BTN_START,  "Return" },
            { "Up",     nes::BTN_UP,     "Up"     },
            { "Down",   nes::BTN_DOWN,   "Down"   },
            { "Left",   nes::BTN_LEFT,   "Left"   },
            { "Right",  nes::BTN_RIGHT,  "Right"  },
    };
    audio_.reserve(kSampleRate / 30);
  }

  const char* name() const override { return "NES"; }
  std::string romInfo() const override {
        if (!cart_.loaded()) return {};
        return "mapper " + std::to_string(cart_.mapperId()) +
               ", PRG " + std::to_string(cart_.prgSize() / 1024) + " KB" +
               ", CHR " + std::to_string(cart_.chrSize() / 1024) + " KB " +
               (cart_.chrIsRam() ? "RAM" : "ROM");
    }

    bool loadRom(const std::string& path, std::string& error) override {
        if (!cart_.load(path, error)) return false;
        bus_.insertCartridge(&cart_);
        bus_.reset();
        audio_.clear();
        audio_accum_ = 0.0;
        return true;
    }

    void reset() override {
        bus_.reset();
        audio_.clear();
        audio_accum_ = 0.0;
    }

    void runFrame() override {
        audio_.clear();
        do {
            const uint64_t before = bus_.masterClock();
            bus_.clock();
            // The CPU ticks on every third master clock; sample there so the
            // rate matches the real 1.79 MHz timebase.
            if (before % 3 == 0) {
                audio_accum_ += 1.0;
                if (audio_accum_ >= kCyclesPerSample) {
                    audio_accum_ -= kCyclesPerSample;
                    audio_.push_back(bus_.audioSample());
                }
            }
        } while (!bus_.ppu.frame_complete);
        bus_.ppu.frame_complete = false;
    }
    VideoFrame video() const override {
        return { bus_.ppu.framebuffer(), nes::kScreenWidth, nes::kScreenHeight };
    }
    
    double refreshRate() const override { return 60.0988; }

     double aspectRatio() const override { return 4.0 / 3.0; }
 
    int sampleRate() const override { return kSampleRate; }
 
    size_t readAudio(float* out, size_t frames) override {
        const size_t n = audio_.size() < frames ? audio_.size() : frames;
        std::memcpy(out, audio_.data(), n * sizeof(float));
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<long>(n));
        return n;
    }
 
    int portCount() const override { return 2; }
 
    const std::vector<ButtonInfo>& buttons() const override { return buttons_; }
 
    void setInput(int port, uint32_t state) override {
        bus_.setController(port, static_cast<uint8_t>(state));
    }
 
private:
    nes::Bus       bus_;
    nes::Cartridge cart_;
    std::vector<ButtonInfo> buttons_;
    std::vector<float>      audio_;
    double audio_accum_ = 0.0;
};
}

SystemPtr createNesSystem() { return std::make_unique<NesSystem>(); }

}
