#include "../../src/core/system.h"
#include "../../src/core/serializer.h"
#include "bus.h"
#include "cartridge.h"
#include <sstream>
#include <cstring>
#include <cstdio>


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

  ~NesSystem() override {
      if (!rom_path_.empty() && !cart_.saveBatterySave(rom_path_)) {
          std::fprintf(stderr, "Could not save NES battery RAM\n");
      }
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
        if (!cart_.loadBatterySave(path)) {
            error = "could not load NES battery save";
            return false;
        }
        rom_path_ = path;
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

    Overscan overscan() const override {
        return {8, 8, 0, 0};
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

    int audioChannelCount() const override { return 4; }

    const char* audioChannelName(int index) const override {
        static const char* kNames[4] = {"Pulse 1", "Pulse 2", "Triangle",
                                        "Noise"};
        return (index >= 0 && index < 4) ? kNames[index] : "";
    }

    void setAudioChannelMask(uint32_t mask) override {
        bus_.apu.setChannelMask(static_cast<uint8_t>(mask & 0x0F));
    }

    uint32_t audioChannelMask() const override {
        return bus_.apu.channelMask();
    }

 
    int portCount() const override { return 2; }
 
    const std::vector<ButtonInfo>& buttons() const override { return buttons_; }
 
    void setInput(int port, uint32_t state) override {
        bus_.setController(port, static_cast<uint8_t>(state));
    }

    bool supportsPatches() const override { return true; }

    void setPatches(const std::vector<MemoryPatch>& patches) override {
        bus_.setPatches(patches);
    }

    bool readMemory(uint32_t address, uint8_t& value) const override {
        if (address > 0xFFFF) return false;

        value = const_cast<NesSystem*>(this)->bus_.cpuRead(
            static_cast<uint16_t>(address), true);
        return true;
    }

    std::vector<MemoryRegion> memoryRegions() const override {
        return {{"RAM", 0x0000, 0x0800}, {"Save RAM", 0x6000, 0x2000}};
    }

    int paletteSize() const override { return 64; }

    bool setPalette(const uint32_t* entries, int count) override {
        if (!entries || count < 64) return false;
        bus_.ppu.setPalette(entries);
        return true;
    }

    bool saveState(std::vector<uint8_t>& out) const override {
        if (!cart_.loaded()) return false;
        StateWriter visit;
        visit(kStateMagic);
        visit(kStateVersion);
        const uint32_t system_id = 1;  // 1 = NES
        visit(system_id);
        const uint32_t mapper_id = static_cast<uint32_t>(cart_.mapperId());
        visit(mapper_id);
        // visitState mutates nothing when the visitor is a writer.
        const_cast<NesSystem*>(this)->bus_.visitState(visit);
        out = visit.take();
        return !out.empty();
    }

    bool loadState(const std::vector<uint8_t>& in) override {
        if (!cart_.loaded() || in.empty()) return false;
        StateReader visit(in.data(), in.size());
        uint32_t magic = 0, version = 0, system_id = 0, mapper_id = 0;
        visit(magic); visit(version); visit(system_id); visit(mapper_id);
        if (!visit.ok() || magic != kStateMagic || version != kStateVersion ||
            system_id != 1 ||
            mapper_id != static_cast<uint32_t>(cart_.mapperId())) {
            return false;
        }
        // Snapshot first: a truncated state must not leave a half-written machine.
        std::vector<uint8_t> rollback;
        if (!saveState(rollback)) return false;
        bus_.visitState(visit);
        if (!visit.ok()) {
            StateReader undo(rollback.data(), rollback.size());
            uint32_t discard = 0;
            undo(discard); undo(discard); undo(discard); undo(discard);
            bus_.visitState(undo);
            return false;
        }
        audio_.clear();
        audio_accum_ = 0.0;
        return true;
    }

    std::string debugInfo() const override {
        std::ostringstream out;
        out << "CPU PC: " << std::hex << bus_.cpu.pc << "\n";
        out << "CPU A:  " << std::hex << static_cast<int>(bus_.cpu.a) << "\n";
        out << "CPU X:  " << std::hex << static_cast<int>(bus_.cpu.x) << "\n";
        out << "CPU Y:  " << std::hex << static_cast<int>(bus_.cpu.y) << "\n";
        out << "CPU P:  " << std::hex << static_cast<int>(bus_.cpu.status) << "\n";
        out << "CPU SP: " << std::hex << static_cast<int>(bus_.cpu.sp) << "\n";
        return out.str();
    }
 
private:
    nes::Bus       bus_;
    nes::Cartridge cart_;
    std::vector<ButtonInfo> buttons_;
    std::vector<float>      audio_;
    double audio_accum_ = 0.0;
    std::string rom_path_;
};
}

SystemPtr createNesSystem() { return std::make_unique<NesSystem>(); }

}
