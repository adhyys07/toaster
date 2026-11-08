#pragma once

#include <cstdint>

namespace emu::frontend {

class Turbo {

public:
    void configure(uint32_t mask, int period) {
        mask_ = mask;
        period_ = period < 2 ? 2 : period;
    }

    void tick() { ++frame_; }
    void reset() { frame_ = 0; }

    // True when a turbo-flagged action should read as released this frame.
    // Half the period down, half up, so the core sees a clean square wave
    // rather than a one-frame blip it might poll straight past.
    bool suppressed(int pad_action) const {
        // Bounded before the shift: shifting by 32 or more is undefined, and
        // the caller passes an index that can grow with the action list.
        if (pad_action < 0 || pad_action >= 32) return false;
        if ((mask_ & (1u << pad_action)) == 0) return false;
        const unsigned period = static_cast<unsigned>(period_);
        return (frame_ % period) >= (period / 2u);
    }

private:
    uint32_t mask_ = 0;
    int period_ = 4;
    unsigned frame_ = 0;
};

}