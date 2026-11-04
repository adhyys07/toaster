#include "apu.h"

#include <algorithm>
#include <cstring>

namespace emu::gb {
namespace {

// Duty waveforms: 12.5%, 25%, 50%, 75%.
constexpr uint8_t kDutyTable[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

constexpr uint16_t kNoiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};

// Bits of each NRxx register that read back as one.
constexpr uint8_t kReadMasks[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,          // FF10-FF14  channel 1
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,          // FF15-FF19  channel 2
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,          // FF1A-FF1E  channel 3
    0xFF, 0xFF, 0x00, 0x00, 0xBF,          // FF1F-FF23  channel 4
    0x00, 0x00, 0x70,                      // FF24-FF26  control
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // FF27-FF2F unused
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // FF30-FF37 wave
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // FF38-FF3F wave
};

constexpr unsigned kCpuHz = 4194304;
constexpr unsigned kSampleRate = 44100;

} // namespace

// --- channel 1 and 2 ---------------------------------------------------------

void APU::Square::clockTimer() {
    if (timer > 0 && --timer != 0) return;
    // A square's period in T-cycles is (2048 - frequency) * 4.
    timer = static_cast<uint16_t>((2048 - (frequency & 0x7FF)) * 4);
    duty_step = static_cast<uint8_t>((duty_step + 1) & 7);
}

void APU::Square::clockLength() {
    if (!length_enabled || length == 0) return;
    if (--length == 0) enabled = false;
}

void APU::Square::clockEnvelope() {
    if (envelope_period == 0) return;
    if (envelope_timer > 0 && --envelope_timer != 0) return;
    envelope_timer = envelope_period;

    if (envelope_up && volume < 15) {
        ++volume;
    } else if (!envelope_up && volume > 0) {
        --volume;
    }
}

void APU::Square::clockSweep() {
    if (!sweep_enabled) return;
    if (sweep_timer > 0 && --sweep_timer != 0) return;
    sweep_timer = sweep_period == 0 ? 8 : sweep_period;
    if (sweep_period == 0) return;

    const uint16_t delta = static_cast<uint16_t>(sweep_shadow >> sweep_shift);
    const int target = sweep_negate ? sweep_shadow - delta
                                    : sweep_shadow + delta;
    if (target > 2047) {
        // Overflowing the frequency silences the channel.
        enabled = false;
        return;
    }
    if (sweep_shift != 0 && target >= 0) {
        sweep_shadow = static_cast<uint16_t>(target);
        frequency = sweep_shadow;
    }
}

void APU::Square::trigger(bool with_sweep) {
    enabled = dac_on;
    if (length == 0) length = 64;
    timer = static_cast<uint16_t>((2048 - (frequency & 0x7FF)) * 4);
    volume = envelope_initial;
    envelope_timer = envelope_period;

    if (with_sweep) {
        sweep_shadow = frequency;
        sweep_timer = sweep_period == 0 ? 8 : sweep_period;
        sweep_enabled = sweep_period != 0 || sweep_shift != 0;
        // A shift of zero still checks for overflow on trigger.
        if (sweep_shift != 0) {
            const uint16_t delta =
                static_cast<uint16_t>(sweep_shadow >> sweep_shift);
            const int target = sweep_negate ? sweep_shadow - delta
                                            : sweep_shadow + delta;
            if (target > 2047) enabled = false;
        }
    }
}

uint8_t APU::Square::output() const {
    if (!enabled || !dac_on) return 0;
    return kDutyTable[duty & 3][duty_step] ? volume : 0;
}

// --- channel 3 ---------------------------------------------------------------

void APU::Wave::clockTimer() {
    if (timer > 0 && --timer != 0) return;
    // The wave channel steps twice as fast as a square.
    timer = static_cast<uint16_t>((2048 - (frequency & 0x7FF)) * 2);
    position = static_cast<uint8_t>((position + 1) & 31);
}

void APU::Wave::clockLength() {
    if (!length_enabled || length == 0) return;
    if (--length == 0) enabled = false;
}

void APU::Wave::trigger() {
    enabled = dac_on;
    if (length == 0) length = 256;
    timer = static_cast<uint16_t>((2048 - (frequency & 0x7FF)) * 2);
    position = 0;
}

uint8_t APU::Wave::output() const {
    if (!enabled || !dac_on || volume_shift == 0) return 0;
    const uint8_t byte = samples[position / 2];
    const uint8_t nibble = (position & 1) ? (byte & 0x0F)
                                         : static_cast<uint8_t>(byte >> 4);
    return static_cast<uint8_t>(nibble >> (volume_shift - 1));
}

// --- channel 4 ---------------------------------------------------------------

void APU::Noise::clockTimer() {
    if (timer > 0 && --timer != 0) return;
    timer = static_cast<uint16_t>(kNoiseDivisors[divisor_code & 7]
                                  << clock_shift);

    // 15-bit LFSR; in short mode the feedback also lands in bit 6.
    const uint16_t feedback =
        static_cast<uint16_t>((shift_register ^ (shift_register >> 1)) & 1);
    shift_register = static_cast<uint16_t>(shift_register >> 1);
    shift_register = static_cast<uint16_t>(shift_register | (feedback << 14));
    if (width_7bit) {
        shift_register = static_cast<uint16_t>((shift_register & ~0x40) |
                                               (feedback << 6));
    }
}

void APU::Noise::clockLength() {
    if (!length_enabled || length == 0) return;
    if (--length == 0) enabled = false;
}

void APU::Noise::clockEnvelope() {
    if (envelope_period == 0) return;
    if (envelope_timer > 0 && --envelope_timer != 0) return;
    envelope_timer = envelope_period;

    if (envelope_up && volume < 15) {
        ++volume;
    } else if (!envelope_up && volume > 0) {
        --volume;
    }
}

void APU::Noise::trigger() {
    enabled = dac_on;
    if (length == 0) length = 64;
    timer = static_cast<uint16_t>(kNoiseDivisors[divisor_code & 7]
                                  << clock_shift);
    shift_register = 0x7FFF;
    volume = envelope_initial;
    envelope_timer = envelope_period;
}

uint8_t APU::Noise::output() const {
    if (!enabled || !dac_on) return 0;
    // The output is the inverse of bit 0.
    return (shift_register & 1) ? 0 : volume;
}

// --- the unit itself ---------------------------------------------------------

APU::APU() {
    reset();
}

void APU::reset() {
    samples_.clear();
    samples_.reserve(4096);

    square1_ = Square{};
    square2_ = Square{};
    wave_ = Wave{};
    noise_ = Noise{};

    left_volume_ = 7;
    right_volume_ = 7;
    panning_ = 0xF3;
    power_ = true;

    frame_counter_ = 0;
    frame_step_ = 0;
    sample_counter_ = 0;
    registers_.fill(0);
}

void APU::powerOff() {
    // Turning NR52's power bit off clears every channel register and holds
    // the channels silent until power returns.
    square1_ = Square{};
    square2_ = Square{};
    noise_ = Noise{};

    // The wave table survives a power cycle; everything else does not.
    uint8_t wave_table[16];
    std::memcpy(wave_table, wave_.samples, sizeof(wave_table));
    wave_ = Wave{};
    std::memcpy(wave_.samples, wave_table, sizeof(wave_table));

    left_volume_ = 0;
    right_volume_ = 0;
    panning_ = 0;
    frame_step_ = 0;
    std::fill(registers_.begin(), registers_.begin() + 0x20, uint8_t{0});
}

void APU::clockFrameSequencer() {
    // 512 Hz, eight steps: length on 0/2/4/6, sweep on 2/6, envelope on 7.
    switch (frame_step_) {
    case 0:
    case 4:
        square1_.clockLength();
        square2_.clockLength();
        wave_.clockLength();
        noise_.clockLength();
        break;
    case 2:
    case 6:
        square1_.clockLength();
        square2_.clockLength();
        wave_.clockLength();
        noise_.clockLength();
        square1_.clockSweep();
        break;
    case 7:
        square1_.clockEnvelope();
        square2_.clockEnvelope();
        noise_.clockEnvelope();
        break;
    default:
        break;
    }

    frame_step_ = (frame_step_ + 1) & 7;
}

float APU::mix() const {
    if (!power_) return 0.0f;

    const int channels[4] = {
        square1_.output(), square2_.output(), wave_.output(), noise_.output()
    };

    // Each channel contributes 0..15 to whichever side NR51 routes it to.
    int left = 0;
    int right = 0;
    for (int i = 0; i < 4; ++i) {
        if (panning_ & (1 << (i + 4))) left += channels[i];
        if (panning_ & (1 << i)) right += channels[i];
    }

    left *= (left_volume_ + 1);
    right *= (right_volume_ + 1);

    // Four channels at 15, times a volume of 8, per side.
    constexpr float kScale = 1.0f / (4 * 15 * 8);
    return static_cast<float>(left + right) * 0.5f * kScale;
}

void APU::tick(unsigned cycles) {
    for (unsigned i = 0; i < cycles; ++i) {
        if (power_) {
            square1_.clockTimer();
            square2_.clockTimer();
            wave_.clockTimer();
            noise_.clockTimer();

            if (++frame_counter_ >= 8192) {   // 4194304 / 512
                frame_counter_ = 0;
                clockFrameSequencer();
            }
        }

        sample_counter_ += kSampleRate;
        if (sample_counter_ >= kCpuHz) {
            sample_counter_ -= kCpuHz;
            if (samples_.size() < 16384) samples_.push_back(mix());
        }
    }
}

size_t APU::readAudio(float* out, size_t frames) {
    const size_t count = std::min(frames, samples_.size());
    std::copy(samples_.begin(), samples_.begin() + count, out);
    samples_.erase(samples_.begin(),
                   samples_.begin() + static_cast<long>(count));
    return count;
}

uint8_t APU::read(uint16_t address) const {
    if (address < 0xFF10 || address > 0xFF3F) return 0xFF;
    const size_t index = address - 0xFF10;

    if (address == 0xFF26) {
        uint8_t status = power_ ? 0x80 : 0x00;
        if (square1_.enabled) status |= 0x01;
        if (square2_.enabled) status |= 0x02;
        if (wave_.enabled) status |= 0x04;
        if (noise_.enabled) status |= 0x08;
        return static_cast<uint8_t>(status | kReadMasks[index]);
    }

    if (address >= 0xFF30) {
        return wave_.samples[address - 0xFF30];
    }

    return static_cast<uint8_t>(registers_[index] | kReadMasks[index]);
}

void APU::write(uint16_t address, uint8_t value) {
    if (address < 0xFF10 || address > 0xFF3F) return;
    const size_t index = address - 0xFF10;

    if (address >= 0xFF30) {
        wave_.samples[address - 0xFF30] = value;
        registers_[index] = value;
        return;
    }

    // With the power off only NR52 and the wave table accept writes.
    if (!power_ && address != 0xFF26) return;

    registers_[index] = value;

    switch (address) {
    // --- channel 1 ---
    case 0xFF10:
        square1_.sweep_period = static_cast<uint8_t>((value >> 4) & 0x07);
        square1_.sweep_negate = (value & 0x08) != 0;
        square1_.sweep_shift = static_cast<uint8_t>(value & 0x07);
        break;
    case 0xFF11:
        square1_.duty = static_cast<uint8_t>(value >> 6);
        square1_.length = static_cast<uint8_t>(64 - (value & 0x3F));
        break;
    case 0xFF12:
        square1_.envelope_initial = static_cast<uint8_t>(value >> 4);
        square1_.envelope_up = (value & 0x08) != 0;
        square1_.envelope_period = static_cast<uint8_t>(value & 0x07);
        // A DAC with no level at all disables the channel outright.
        square1_.dac_on = (value & 0xF8) != 0;
        if (!square1_.dac_on) square1_.enabled = false;
        break;
    case 0xFF13:
        square1_.frequency =
            static_cast<uint16_t>((square1_.frequency & 0x700) | value);
        break;
    case 0xFF14:
        square1_.frequency = static_cast<uint16_t>(
            (square1_.frequency & 0x0FF) | ((value & 0x07) << 8));
        square1_.length_enabled = (value & 0x40) != 0;
        if (value & 0x80) square1_.trigger(true);
        break;

    // --- channel 2 ---
    case 0xFF16:
        square2_.duty = static_cast<uint8_t>(value >> 6);
        square2_.length = static_cast<uint8_t>(64 - (value & 0x3F));
        break;
    case 0xFF17:
        square2_.envelope_initial = static_cast<uint8_t>(value >> 4);
        square2_.envelope_up = (value & 0x08) != 0;
        square2_.envelope_period = static_cast<uint8_t>(value & 0x07);
        square2_.dac_on = (value & 0xF8) != 0;
        if (!square2_.dac_on) square2_.enabled = false;
        break;
    case 0xFF18:
        square2_.frequency =
            static_cast<uint16_t>((square2_.frequency & 0x700) | value);
        break;
    case 0xFF19:
        square2_.frequency = static_cast<uint16_t>(
            (square2_.frequency & 0x0FF) | ((value & 0x07) << 8));
        square2_.length_enabled = (value & 0x40) != 0;
        if (value & 0x80) square2_.trigger(false);
        break;

    // --- channel 3 ---
    case 0xFF1A:
        wave_.dac_on = (value & 0x80) != 0;
        if (!wave_.dac_on) wave_.enabled = false;
        break;
    case 0xFF1B:
        wave_.length = static_cast<uint16_t>(256 - value);
        break;
    case 0xFF1C:
        // 0 mutes; 1, 2 and 3 mean full, half and a quarter volume.
        wave_.volume_shift = static_cast<uint8_t>((value >> 5) & 0x03);
        break;
    case 0xFF1D:
        wave_.frequency =
            static_cast<uint16_t>((wave_.frequency & 0x700) | value);
        break;
    case 0xFF1E:
        wave_.frequency = static_cast<uint16_t>(
            (wave_.frequency & 0x0FF) | ((value & 0x07) << 8));
        wave_.length_enabled = (value & 0x40) != 0;
        if (value & 0x80) wave_.trigger();
        break;

    // --- channel 4 ---
    case 0xFF20:
        noise_.length = static_cast<uint8_t>(64 - (value & 0x3F));
        break;
    case 0xFF21:
        noise_.envelope_initial = static_cast<uint8_t>(value >> 4);
        noise_.envelope_up = (value & 0x08) != 0;
        noise_.envelope_period = static_cast<uint8_t>(value & 0x07);
        noise_.dac_on = (value & 0xF8) != 0;
        if (!noise_.dac_on) noise_.enabled = false;
        break;
    case 0xFF22:
        noise_.clock_shift = static_cast<uint8_t>(value >> 4);
        noise_.width_7bit = (value & 0x08) != 0;
        noise_.divisor_code = static_cast<uint8_t>(value & 0x07);
        break;
    case 0xFF23:
        noise_.length_enabled = (value & 0x40) != 0;
        if (value & 0x80) noise_.trigger();
        break;

    // --- control ---
    case 0xFF24:
        right_volume_ = static_cast<uint8_t>(value & 0x07);
        left_volume_ = static_cast<uint8_t>((value >> 4) & 0x07);
        break;
    case 0xFF25:
        panning_ = value;
        break;
    case 0xFF26: {
        const bool power = (value & 0x80) != 0;
        if (!power && power_) {
            powerOff();
        } else if (power && !power_) {
            frame_step_ = 0;
            frame_counter_ = 0;
        }
        power_ = power;
        break;
    }
    default:
        break;
    }
}

} // namespace emu::gb
