#pragma once

#include "memory.h"

#include <cstdint>

namespace emu::gb {

class Timer {
public:
    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(divider_counter_); visit(timer_counter_);
    }
    explicit Timer(Memory& memory);

    void reset();
    void tick(unsigned cycles);

private:
    void incrementDivider();
    void incrementTimer();

    Memory& memory_;
    uint16_t divider_counter_ = 0;
    uint16_t timer_counter_ = 0;
};

}