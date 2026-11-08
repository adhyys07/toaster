#include "audio_sync.h"

#include <algorithm>

namespace emu::frontend {
namespace {

constexpr double kMaxRatio = 1.005;
constexpr double kMinRatio = 0.995;

constexpr double kGain = 0.05;

} // namespace

void AudioSync::configure(int sample_rate, int buffer_samples) {
    (void)sample_rate;
    target_samples_ =
        static_cast<std::size_t>(std::max(256, buffer_samples) * 2);
    ratio_ = 1.0;
    position_ = 0.0;
    resync_ = false;
}

void AudioSync::process(const float* in, std::size_t count,
                        std::size_t queued_bytes, std::vector<float>& out) {
    out.clear();
    resync_ = false;
    if (!in || count == 0) return;

    const double queued =
        static_cast<double>(queued_bytes / sizeof(float));
    const double target = static_cast<double>(target_samples_);

    if (queued > target * 4.0) {
        out.assign(in, in + count);
        resync_ = true;
        position_ = 0.0;
        return;
    }

    const double error = (queued - target) / target;
    ratio_ = std::clamp(1.0 + error * kGain, kMinRatio, kMaxRatio);

    out.reserve(static_cast<std::size_t>(
                    static_cast<double>(count) / ratio_) + 2);

    double position = position_;
    while (position < static_cast<double>(count)) {
        const std::size_t index = static_cast<std::size_t>(position);
        const double fraction = position - static_cast<double>(index);
        const float a = in[index];
        const float b = (index + 1 < count) ? in[index + 1] : in[count - 1];
        out.push_back(static_cast<float>(a + (b - a) * fraction));
        position += ratio_;
    }
    position_ = position - static_cast<double>(count);
}

} // namespace emu::frontend
