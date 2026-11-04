#include "game_boy_system.h"

#include "../../src/core/serializer.h"
#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

#include <cstdio>
#include <iomanip>
#include <sstream>

namespace emu{
namespace{

// One frame is 154 lines of 456 dots.
constexpr unsigned kCyclesPerFrame = 70224;

class GameBoySystem final : public System {
public:
    GameBoySystem()
        : memory_(),
          timer_(memory_),
          ppu_(memory_),
          apu_(),
          cpu_(memory_, timer_, ppu_, apu_) {
        memory_.connect(&cartridge_);
        memory_.connect(&ppu_, &apu_, &timer_);
    }

    ~GameBoySystem() override {
        if (!rom_path_.empty() && !cartridge_.saveBattery(rom_path_)) {
            std::fprintf(stderr, "Could not save Game Boy cartridge RAM\n");
        }
    }

    const char* name() const override { return "GAME BOY"; }

    std::string romInfo() const override {
        if (!cartridge_.loaded()) return {};
        std::string info = cartridge_.title();
        if (cartridge_.hasBattery()) info += " (battery)";
        return info;
    }

    bool loadRom(const std::string& path, std::string& error) override {
        if (!cartridge_.load(path, error)) {
            return false;
        }

        reset();
        if (!cartridge_.loadBattery(path)) {
            error = "could not load Game Boy cartridge RAM";
            return false;
        }

        rom_path_ = path;
        return true;
    }

    void reset() override {
        memory_.reset();
        cartridge_.reset();
        timer_.reset();
        ppu_.reset();
        apu_.reset();
        cpu_.reset();
    }

    void runFrame() override {
        ppu_.clearFrameComplete();

        // Normally the PPU entering vblank ends the frame. The cycle cap is a
        // backstop for a ROM that switches the LCD off, where the controller
        // never reaches vblank at all.
        unsigned cycles = 0;
        while (!ppu_.frameComplete() && cycles < kCyclesPerFrame) {
            const unsigned executed = cpu_.step();
            cycles += executed > 0 ? executed : 4;
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
        if (port != 0) return;

        // The shell reports A, B, Start, Select, Up, Down, Left, Right in bits
        // 0..7. The hardware register splits them into two nibbles, directions
        // low and actions high, where a pressed key reads as zero -- so this
        // is a remap, not just an inversion.
        uint8_t joypad = 0xFF;
        const auto press = [&](int shell_bit, int hardware_bit) {
            if (state & (1u << shell_bit)) {
                joypad = static_cast<uint8_t>(joypad & ~(1u << hardware_bit));
            }
        };

        press(7, 0);  // Right
        press(6, 1);  // Left
        press(4, 2);  // Up
        press(5, 3);  // Down
        press(0, 4);  // A
        press(1, 5);  // B
        press(3, 6);  // Select
        press(2, 7);  // Start

        cpu_.setInput(joypad);
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
        return true;
    }

    std::string debugInfo() const override {
        std::ostringstream stream;
        stream << "Game Boy: " << romInfo() << "\n"
               << std::hex << std::uppercase << std::setfill('0')
               << "PC=" << std::setw(4) << cpu_.pc()
               << " SP=" << std::setw(4) << cpu_.sp()
               << " AF=" << std::setw(2) << static_cast<unsigned>(cpu_.a())
               << std::setw(2) << static_cast<unsigned>(cpu_.f())
               << " BC=" << std::setw(2) << static_cast<unsigned>(cpu_.b())
               << std::setw(2) << static_cast<unsigned>(cpu_.c())
               << " DE=" << std::setw(2) << static_cast<unsigned>(cpu_.d())
               << std::setw(2) << static_cast<unsigned>(cpu_.e())
               << " HL=" << std::setw(2) << static_cast<unsigned>(cpu_.h())
               << std::setw(2) << static_cast<unsigned>(cpu_.l())
               << std::dec
               << "\nLY=" << static_cast<unsigned>(ppu_.line())
               << " mode=" << static_cast<unsigned>(ppu_.mode())
               << (cpu_.halted() ? " halted" : "");
        return stream.str();
    }

private:
    // The APU's only remaining state is a queue of already-generated samples,
    // which a load drops rather than restores.
    template <typename Visitor>
    void visitMachine(Visitor& visit) {
        cpu_.visitState(visit);
        memory_.visitState(visit);
        timer_.visitState(visit);
        ppu_.visitState(visit);
        apu_.visitState(visit);
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

    // Declaration order is construction order: everything the CPU refers to
    // has to exist before it does.
    gb::Cartridge cartridge_;
    gb::Memory memory_;
    gb::Timer timer_;
    gb::PPU ppu_;
    gb::APU apu_;
    gb::CPU cpu_;

    std::string rom_path_;

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
