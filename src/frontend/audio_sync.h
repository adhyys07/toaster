#pragma once

#include <cstddef>
#include <vector>

namespace emu::frontend {

class AudioSync {
public:
    void configure(int sample_rate, int buffer_samples);

    void process(const float* in, std::size_t count,
                 std::size_t queued_bytes, std::vector<float>& out);

    bool needsResync() const { return resync_; }
    double ratio() const { return ratio_; }


private:
    std::size_t target_samples_ = 2048;
    double ratio_ = 1.0;
    double position_ = 0.0;
    bool resync_ = false;
};

}