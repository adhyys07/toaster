#include "game_boy_system.h"

#include <algorithm>
#include <cstring>

namespace emu{
namespace{

class GameBoySystem final : public System {
public:
    const char* name() const override { return "GAME BOY"; }
    bool loadRom(const std::string& path, std::string& error) override {
        error = "Game Boy CPU and PPU are not implemented yet";
        return false;
    }

    void reset() override {}
    void runFrame() override {}

    VideoFrame video() const override { return {nullptr, 160, 144}; }
    double refreshRate() const override { return 59.7275; }
    double aspectRatio() const override { return 160.0 / 144.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(float*, size_t) override {
        return 0;
    }
    
    const std::vector<ButtonInfo>& buttons() const override {
        return buttons_;
    }

    void setInput(int, uint32_t) override {}

private:
    std::vector<ButtonInfo> buttons_{
        {"A", 1u << 0, "Z"},
        {"B", 1u << 1, "X"},
        {"Start", 1u << 2, "Return"},
        {"Select", 1u << 3, "Right Shift"},
        {"Up", 1u << 4, "Up"},
        {"Down", 1u << 5, "Down"},
        {"Left", 1u << 6, "Left"},
        {"Right", 1u << 7, "Right"}
    };
};

}

SystemPtr createGameBoySystem() {
    return std::make_unique<GameBoySystem>();
}

}