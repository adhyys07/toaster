#include "apu.h"

#include <cmath>

namespace emu::gb {

APU::APU(Memory& memory)
    : memory_(memory) {
    reset();
}

void APU::reset() {
    samples_.clear();
    phase_ = 0.0;
}

void APU::tick(unsigned cycles) {
    constexpr double cpu_hz = 4194304.0;
    constexpr double sample_rate = 44100.0;
    constexpr double frequency = 440.0;

    const size_t count = static_cast<size_t>(
        static_cast<double>(cycles) * sample_rate / cpu_hz);

    const bool enabled = (memory_.read(0xFF26) & 0x80) != 0;

    for (size_t i = 0; i < count; ++i) {
        float sample = 0.0f;

        if (enabled) {
            sample = phase_ < 0.5 ? 0.04f : -0.04f;
            phase_ += frequency / sample_rate;

            if (phase_ >= 1.0) {
                phase_ -= 1.0;
            }
        }

        samples_.push_back(sample);
    }
}

size_t APU::readAudio(float* output, size_t frames) {
    const size_t count =
        samples_.size() < frames ? samples_.size() : frames;

    for (size_t i = 0; i < count; ++i) {
        output[i] = samples_[i];
    }

    samples_.erase(samples_.begin(),
                   samples_.begin() + static_cast<long>(count));

    return count;
}

} // namespace emu::gb