#include "timer.h"

namespace emu::gb {

namespace {

uint16_t timerPeriod(uint8_t tac) {
    switch (tac & 0x03) {
    case 0:
        return 1024;
    case 1:
        return 16;   
    case 2:
        return 64;  
    case 3:
        return 256;  
    default:
        return 1024;
    }
}

} // namespace

Timer::Timer(Memory& memory)
    : memory_(memory) {
    reset();
}

void Timer::reset() {
    divider_counter_ = 0;
    timer_counter_ = 0;

    memory_.write(0xFF04, 0);
    memory_.write(0xFF05, 0);
    memory_.write(0xFF06, 0);
    memory_.write(0xFF07, 0);
}

void Timer::incrementDivider() {
    ++divider_counter_;

    if (divider_counter_ >= 256) {
        divider_counter_ = 0;
        memory_.write(0xFF04,
                      static_cast<uint8_t>(memory_.read(0xFF04) + 1));
    }
}

void Timer::incrementTimer() {
    const uint8_t tac = memory_.read(0xFF07);

    if ((tac & 0x04) == 0) {
        return;
    }

    ++timer_counter_;

    if (timer_counter_ < timerPeriod(tac)) {
        return;
    }

    timer_counter_ = 0;

    uint8_t tima = memory_.read(0xFF05);

    if (tima == 0xFF) {
        memory_.write(0xFF05, memory_.read(0xFF06));
        memory_.requestInterrupt(0x04);
    } else {
        memory_.write(0xFF05, static_cast<uint8_t>(tima + 1));
    }
}

void Timer::tick(unsigned cycles) {
    for (unsigned i = 0; i < cycles; ++i) {
        incrementDivider();
        incrementTimer();
    }
}

} // namespace emu::gb