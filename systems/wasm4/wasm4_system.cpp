#include "wasm4.h"

#include "../../src/core/system.h"

#include <cstdio>
#include <sstream>

namespace emu {
namespace {

using emu::wasm4::Console;
using emu::wasm4::kMemorySize;
using emu::wasm4::kScreenHeight;
using emu::wasm4::kScreenWidth;
constexpr int kSampleRate = 44100;

class Wasm4System final : public System {
public:
    Wasm4System() {
        static const char* kKeys[6] = {"X", "Z", "Left", "Right", "Up",
                                       "Down"};
        static const char* kNames[6] = {"Button 1", "Button 2", "Left",
                                        "Right", "Up", "Down"};
        for (int i = 0; i < 6; ++i) {
            buttons_.push_back({kNames[i], 1u << i, kKeys[i]});
        }
    }

    const char* name() const override { return "WASM-4"; }

    std::string romInfo() const override {
        std::ostringstream out;
        out << "WASM-4 cartridge, " << kScreenWidth << "x" << kScreenHeight;
        return out.str();
    }

    bool loadRom(const std::string& path, std::string& error) override {
        return console_.loadRom(path, error);
    }

    void reset() override { console_.reset(); }
    void runFrame() override { console_.runFrame(); }

    VideoFrame video() const override {
        return {console_.framebuffer(), kScreenWidth, kScreenHeight};
    }

    double refreshRate() const override { return 60.0; }
    double aspectRatio() const override { return 1.0; }
    int sampleRate() const override { return kSampleRate; }

    size_t readAudio(float* out, size_t frames) override {
        if (!out) return 0;
        const size_t wanted = static_cast<size_t>(kSampleRate / 60);
        const size_t count = frames < wanted ? frames : wanted;
        for (size_t i = 0; i < count; ++i) out[i] = 0.0f;
        return count;
    }

    const std::vector<ButtonInfo>& buttons() const override {
        return buttons_;
    }

    int portCount() const override { return 4; }

    void setInput(int port, uint32_t state) override {
        console_.setGamepad(port, static_cast<uint8_t>(state & 0x3F));
    }

    bool saveState(std::vector<uint8_t>& out) const override {
        out.resize(kMemorySize + 1);
        for (uint32_t i = 0; i < kMemorySize; ++i) {
            out[i] = console_.peek(i);
        }
        out[kMemorySize] = console_.started() ? 1 : 0;
        return true;
    }

    bool loadState(const std::vector<uint8_t>& in) override {
        if (in.size() != kMemorySize + 1) return false;
        return console_.restore(in.data(), kMemorySize,
                                in[kMemorySize] != 0);
    }

    bool readMemory(uint32_t address, uint8_t& value) const override {
        if (address >= kMemorySize) return false;
        value = console_.peek(address);
        return true;
    }

    std::vector<MemoryRegion> memoryRegions() const override {
        return {{"RAM", 0x19A0, kMemorySize - 0x19A0}};
    }

    std::string debugInfo() const override {
        std::ostringstream out;
        out << "WASM-4  started=" << (console_.started() ? "yes" : "no");
        return out.str();
    }

private:
    Console console_;
    std::vector<ButtonInfo> buttons_;
};

} // namespace

SystemPtr createWasm4System() {
    return std::make_unique<Wasm4System>();
}

} // namespace emu
