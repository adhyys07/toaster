#pragma once

#include "memory.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emu::gb {

class APU {
public:
    explicit APU(Memory& memory);

    void reset();
    void tick(unsigned cycles);

    size_t readAudio(float* out, size_t frames);
    int sampleRate() const { return 44100; }

private:
    Memory& memory_;
    std::vector<float> samples_;
    double phase_ = 0.0;
};

}