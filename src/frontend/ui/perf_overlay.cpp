#include "perf_overlay.h"

#include <algorithm>
#include <cstdio>

namespace emu::frontend::ui {

void PerfOverlay::reset() {
    history_.fill(0.0f);
    next_ = 0;
    filled_ = 0;
}

void PerfOverlay::sample(double frame_seconds) {
    history_[next_] = static_cast<float>(frame_seconds);
    next_ = (next_ + 1) % kSamples;
    if (filled_ < kSamples) ++filled_;
}

void PerfOverlay::draw(SDL_Renderer* renderer, Painter& painter,
                       TextCache& text, const Theme& theme,
                       const Metrics& metrics, const SDL_Rect& window,
                       double target_seconds) const {
    if (filled_ == 0) return;
    if (target_seconds <= 0.0) target_seconds = 1.0 / 60.0;

    const int line = text.lineHeight(FontRole::Small);
    const int width = metrics.px(180);
    const int graph_h = metrics.px(40);
    const SDL_Rect panel{window.x + window.w - width - metrics.pad(),
                         window.y + metrics.pad(), width,
                         graph_h + line * 2 + metrics.px(18)};

    // Its own translucent black rather than theme.surface: this sits over the
    // game, and an opaque panel would hide more of it than it needs to.
    painter.roundedRect(panel, metrics.radius_small(),
                        SDL_Color{0, 0, 0, 170});

    // The mean tells you the speed; the peak tells you whether it stutters.
    // Reporting only one of the two is what made the old counter useless.
    double total = 0.0;
    float worst = 0.0f;
    for (std::size_t i = 0; i < filled_; ++i) {
        total += history_[i];
        worst = std::max(worst, history_[i]);
    }
    const double mean = total / static_cast<double>(filled_);

    char caption[64] = {};
    std::snprintf(caption, sizeof(caption), "%.1f fps   %.2f ms",
                  mean > 0.0 ? 1.0 / mean : 0.0, mean * 1000.0);
    text.draw(renderer, FontRole::Small, caption, panel.x + metrics.px(8),
              panel.y + metrics.px(6), theme.text);

    char peak[64] = {};
    std::snprintf(peak, sizeof(peak), "peak %.2f ms", worst * 1000.0);
    text.draw(renderer, FontRole::Small, peak, panel.x + metrics.px(8),
              panel.y + metrics.px(6) + line,
              worst > target_seconds * 1.5 ? theme.danger : theme.text_muted);

    const SDL_Rect graph{panel.x + metrics.px(8),
                         panel.y + metrics.px(12) + line * 2,
                         panel.w - metrics.px(16), graph_h};
    painter.rect(graph, SDL_Color{0, 0, 0, 90});

    // Full height is twice the frame budget, so a frame exactly on budget
    // sits at the midline and a doubled frame tops out instead of clipping.
    const double full = target_seconds * 2.0;

    // The budget line, so a bar crossing it explains itself without a legend.
    painter.hline(graph.x, graph.y + graph.h / 2, graph.w, 1,
                  theme.text_muted);

    const int bar_w = std::max(1, graph.w / static_cast<int>(kSamples));
    for (std::size_t i = 0; i < filled_; ++i) {
        // Oldest first, left to right, so the graph scrolls the way people
        // expect rather than jumping where the ring buffer wraps.
        const std::size_t index = (next_ + kSamples - filled_ + i) % kSamples;
        const double value = history_[index];

        const int h = std::clamp(
            static_cast<int>(value / full * graph.h), 1, graph.h);
        const SDL_Color colour =
            value > target_seconds * 1.5
                ? theme.danger
                : (value > target_seconds ? theme.gold : theme.success);

        painter.rect(SDL_Rect{graph.x + static_cast<int>(i) * bar_w,
                              graph.y + graph.h - h, bar_w, h},
                     colour);
    }
}

} // namespace emu::frontend::ui
