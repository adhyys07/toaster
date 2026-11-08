#pragma once

#include "core/system.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::frontend {

class RunAhead {
public:
    bool prepare(System& system, int frames, double refresh_rate, std::string& error);

    void disable();
    bool enabled() const { return frames_ > 1; }
    int frames() const { return frames_; }

    double cost() const { return cost_; }

    bool runFrame(System& system, std::vector<float>& audio_out);

    VideoFrame frame(const System& system) const;

private:
    std::vector<uint8_t> state_;
    std::vector<uint32_t> pixels_;
    std::vector<float> discard_;
    int width_ = 0;
    int height_ = 0;
    int frames_ = 0;
    double cost_ = 0.0;
};
   
}