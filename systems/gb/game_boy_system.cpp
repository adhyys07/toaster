#include "game_boy_system.h"

#include "../../src/core/serializer.h"
#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

#include <iomanip>
#include <sstream>

namespace emu{
namespace{

class GameBoySystem final : public System {
public:
    GameBoySystem()
        : memory_(),
          timer_(memory_),
          cpu_(memory_, timer_),
          ppu_(memory_),
          apu_(memory_) {
        memory_.connect(&cartridge_);
    }

    const char* name() const override { return "GAME BOY"; }

    std::string romInfo() const override {
        return cartridge_.loaded() ? cartridge_.title() : std::string{};
    }

    bool loadRom(const std::string& path, std::string& error) override {
        if (!cartridge_.load(path, error)) {
            return false;
        }

        reset();
        return true;
    }

    void reset() override {
        memory_.reset();
        cartridge_.reset();
        timer_.reset();
        cpu_.reset();
        ppu_.reset();
        apu_.reset();
    }

    void runFrame() override {
        constexpr unsigned cyclesPerFrame = 70224;
        unsigned cycles = 0;

        while (cycles < cyclesPerFrame) {
            const unsigned executed = cpu_.step();
            ppu_.tick(executed);
            apu_.tick(executed);
            cycles += executed;
        }
    }

    VideoFrame video() const override {
        return {ppu_.framebuffer(), 160, 144};
    }

    double refreshRate() const override { return 59.7275; }
    double aspectRatio() const override { return 160.0 / 144.0; }
    int sampleRate() const override { return apu_.sampleRate(); }

    size_t readAudio(float* out, size_t frames) override {
        return apu_.readAudio(out, frames);
    }

    const std::vector<ButtonInfo>& buttons() const override {
        return buttons_;
    }

    void setInput(int port, uint32_t state) override {
        if (port == 0) {
            cpu_.setInput(static_cast<uint8_t>(~state));
        }
    }

    bool saveState(std::vector<uint8_t>& out) const override {
        if (!cartridge_.loaded()) return false;

        StateWriter visit;
        visit(kStateMagic);
        visit(kStateVersion);
        const uint32_t system_id = 2;  // 2 = Game Boy
        visit(system_id);
        const uint32_t cartridge_id = cartridgeId();
        visit(cartridge_id);
        // visitState mutates nothing when the visitor is a writer.
        const_cast<GameBoySystem*>(this)->visitMachine(visit);
        out = visit.take();
        return !out.empty();
    }

    bool loadState(const std::vector<uint8_t>& in) override {
        if (!cartridge_.loaded() || in.empty()) return false;

        StateReader visit(in.data(), in.size());
        uint32_t magic = 0, version = 0, system_id = 0, cartridge_id = 0;
        visit(magic); visit(version); visit(system_id); visit(cartridge_id);
        if (!visit.ok() || magic != kStateMagic || version != kStateVersion ||
            system_id != 2 || cartridge_id != cartridgeId()) {
            return false;
        }

        // Snapshot first: a truncated state must not leave a half-written
        // machine behind.
        std::vector<uint8_t> rollback;
        if (!saveState(rollback)) return false;

        visitMachine(visit);
        if (!visit.ok()) {
            StateReader undo(rollback.data(), rollback.size());
            uint32_t discard = 0;
            undo(discard); undo(discard); undo(discard); undo(discard);
            visitMachine(undo);
            return false;
        }

        // The APU holds a queue of generated samples that no longer matches
        // the restored machine, so drop it.
        apu_.reset();
        return true;
    }

    std::string debugInfo() const override {
        std::ostringstream stream;
        stream << "Game Boy: " << romInfo() << "\n"
               << "PC=" << std::hex << std::uppercase
               << std::setw(4) << std::setfill('0') << cpu_.pc()
               << " SP=" << std::setw(4) << cpu_.sp()
               << " AF=" << std::setw(2) << static_cast<unsigned>(cpu_.a())
               << std::setw(2) << static_cast<unsigned>(cpu_.f())
               << " BC=" << std::setw(2) << static_cast<unsigned>(cpu_.b())
               << std::setw(2) << static_cast<unsigned>(cpu_.c())
               << " DE=" << std::setw(2) << static_cast<unsigned>(cpu_.d())
               << std::setw(2) << static_cast<unsigned>(cpu_.e())
               << " HL=" << std::setw(2) << static_cast<unsigned>(cpu_.h())
               << std::setw(2) << static_cast<unsigned>(cpu_.l());
        return stream.str();
    }

private:
    // The APU is excluded: its only state is a queue of already-generated
    // samples, which loadState drops instead of restoring.
    template <typename Visitor>
    void visitMachine(Visitor& visit) {
        cpu_.visitState(visit);
        memory_.visitState(visit);
        timer_.visitState(visit);
        ppu_.visitState(visit);
        cartridge_.visitState(visit);
    }

    // FNV-1a over the cartridge title, so a state saved in one game is
    // rejected when loaded into another.
    uint32_t cartridgeId() const {
        uint32_t hash = 2166136261u;
        for (char ch : cartridge_.title())
            hash = (hash ^ static_cast<uint8_t>(ch)) * 16777619u;
        return hash;
    }

    gb::Cartridge cartridge_;
    gb::Memory memory_;
    gb::Timer timer_;
    gb::CPU cpu_;
    gb::PPU ppu_;
    gb::APU apu_;

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