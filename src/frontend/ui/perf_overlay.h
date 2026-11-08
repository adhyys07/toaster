#pragma once

#include "painter.h"
#include "text_cache.h"
#include "theme.h"

#include <SDL.h>

#include <array>
#include <cstddef>

namespace emu::frontend::ui {

// A frame-time graph and counters, drawn over the game.
//
// The title-bar FPS counter could only report an average over a whole second,
// which hides the thing that actually matters: one long frame. Run-ahead and
// the audio resampler both change frame cost, and until now neither was
// observable.
class PerfOverlay {
public:
    // Called once per displayed frame with how long that frame's work took.
    void sample(double frame_seconds);

    void draw(SDL_Renderer* renderer, Painter& painter, TextCache& text,
              const Theme& theme, const Metrics& metrics,
              const SDL_Rect& window, double target_seconds) const;

    void reset();

    bool empty() const { return filled_ == 0; }

private:
    // Two seconds at 60Hz. Long enough to show a pattern, short enough that a
    // single bad frame is still visible rather than averaged away.
    static constexpr std::size_t kSamples = 120;

    std::array<float, kSamples> history_{};
    std::size_t next_ = 0;
    std::size_t filled_ = 0;
};

} // namespace emu::frontend::ui
