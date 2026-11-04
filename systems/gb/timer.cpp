#include "timer.h"

#include "memory.h"

namespace emu::gb {
namespace {

// Which bit of the 16-bit divider TIMA follows, indexed by TAC's low two bits:
// 4096 Hz, 262144 Hz, 65536 Hz, 16384 Hz.
constexpr int kSelectedBit[4] = {9, 3, 5, 7};

} // namespace

Timer::Timer(Memory& memory)
    : memory_(memory) {
    reset();
}

void Timer::reset() {
    counter_ = 0;
    tima_ = 0;
    tma_ = 0;
    tac_ = 0;
    reload_delay_ = 0;
}

bool Timer::selectedBit() const {
    if ((tac_ & 0x04) == 0) return false;
    return ((counter_ >> kSelectedBit[tac_ & 0x03]) & 1) != 0;
}

void Timer::detectFallingEdge(bool previous) {
    if (!previous || selectedBit()) return;

    if (tima_ == 0xFF) {
        tima_ = 0;
        reload_delay_ = 4;
    } else {
        ++tima_;
    }
}

void Timer::tick(unsigned cycles) {
    for (unsigned i = 0; i < cycles; ++i) {
        if (reload_delay_ > 0 && --reload_delay_ == 0) {
            tima_ = tma_;
            memory_.requestInterrupt(0x04);
        }

        const bool previous = selectedBit();
        ++counter_;
        detectFallingEdge(previous);
    }
}

uint8_t Timer::read(uint16_t address) const {
    switch (address) {
    case 0xFF04: return static_cast<uint8_t>(counter_ >> 8);
    case 0xFF05: return tima_;
    case 0xFF06: return tma_;
    // The unused bits of TAC read as one.
    case 0xFF07: return static_cast<uint8_t>(0xF8 | (tac_ & 0x07));
    default:     return 0xFF;
    }
}

void Timer::write(uint16_t address, uint8_t value) {
    switch (address) {
    case 0xFF04: {
        // Resetting the counter can itself clear the selected bit.
        const bool previous = selectedBit();
        counter_ = 0;
        detectFallingEdge(previous);
        break;
    }
    case 0xFF05:
        // A write inside the reload window cancels the pending reload.
        tima_ = value;
        reload_delay_ = 0;
        break;
    case 0xFF06:
        tma_ = value;
        break;
    case 0xFF07: {
        const bool previous = selectedBit();
        tac_ = value & 0x07;
        detectFallingEdge(previous);
        break;
    }
    default:
        break;
    }
}

} // namespace emu::gb
