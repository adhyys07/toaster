#include "run_ahead.h"

#include <SDL.h>

#include <algorithm>

namespace emu::frontend {
namespace {

constexpr std::size_t kAudioChunk = 8192;
constexpr double kBudgetLimit = 0.60;

} // namespace

void RunAhead::disable() {
    frames_ = 0;
    cost_ = 0.0;
    state_.clear();
    pixels_.clear();
}

bool RunAhead::prepare(System& system, int frames, double refresh_rate,
                       std::string& error) {
    disable();
    if (frames <= 1) {
        error.clear();
        return false;
    }

    std::vector<uint8_t> probe;
    if (!system.saveState(probe) || probe.empty()) {
        error = "This core has no save states, which run-ahead needs.";
        return false;
    }

    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const Uint64 start = SDL_GetPerformanceCounter();

    std::vector<uint8_t> scratch;
    if (!system.saveState(scratch)) {
        error = "This core could not save a state.";
        return false;
    }
    system.runFrame();
    if (!system.loadState(scratch)) {
        error = "This core cannot reload its own save state.";
        return false;
    }

    const double round_trip =
        static_cast<double>(SDL_GetPerformanceCounter() - start) /
        static_cast<double>(frequency);

    const double budget = refresh_rate > 0.0 ? 1.0 / refresh_rate : 1.0 / 60.0;
    cost_ = (round_trip + round_trip * (frames - 2) * 0.5) / budget;
    if (cost_ > kBudgetLimit) {
        error = "Run-ahead would use about " +
                std::to_string(static_cast<int>(cost_ * 100)) +
                "% of each frame on this core.";
        return false;
    }

    frames_ = std::clamp(frames, 2, 8);
    discard_.assign(kAudioChunk, 0.0f);
    error.clear();
    return true;
}

bool RunAhead::runFrame(System& system, std::vector<float>& audio_out) {
    audio_out.clear();

    if (frames_ <= 1) {
        system.runFrame();
        audio_out.resize(kAudioChunk);
        audio_out.resize(system.readAudio(audio_out.data(), audio_out.size()));
        return true;
    }

    system.runFrame();

    audio_out.resize(kAudioChunk);
    audio_out.resize(system.readAudio(audio_out.data(), audio_out.size()));

    if (!system.saveState(state_) || state_.empty()) return false;

    for (int i = 1; i < frames_; ++i) {
        system.runFrame();
        while (system.readAudio(discard_.data(), discard_.size()) ==
               discard_.size()) {
        }
    }

    const VideoFrame predicted = system.video();
    if (predicted.pixels && predicted.width > 0 && predicted.height > 0) {
        width_ = predicted.width;
        height_ = predicted.height;
        const std::size_t count =
            static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        pixels_.assign(predicted.pixels, predicted.pixels + count);
    }

    return system.loadState(state_);
}

VideoFrame RunAhead::frame(const System& system) const {
    if (frames_ <= 1 || pixels_.empty()) return system.video();
    return VideoFrame{pixels_.data(), width_, height_};
}

} // namespace emu::frontend
