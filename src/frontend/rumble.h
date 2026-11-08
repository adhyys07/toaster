#pragma once

#include <SDL.h>

#include <cstdint>

namespace emu::frontend {

class Rumble {
public:
    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    void pulse(SDL_GameController* pad, float strength, uint32_t ms) {
        if (!enabled_ || !pad || strength <= 0.0f) return;
        const Uint16 level = static_cast<Uint16>(
            (strength > 1.0f ? 1.0f : strength) * 0xFFFF);
        // Both motors together: a single-motor pad ignores the one it lacks,
        // and driving only one feels lopsided on pads that have two. The
        // return value is discarded on purpose, because a pad without motors
        // reports failure and that is not a problem worth surfacing.
        SDL_GameControllerRumble(pad, level, level, ms);
    }

    void stop(SDL_GameController* pad) {
        if (pad) SDL_GameControllerRumble(pad, 0, 0, 0);
    }

private:
    bool enabled_ = true;
};

}