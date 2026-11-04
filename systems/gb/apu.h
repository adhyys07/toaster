#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emu::gb {

class APU {
public:
    APU();

    void reset();
    void tick(unsigned cycles);

    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    size_t readAudio(float* out, size_t frames);
    int sampleRate() const { return 44100; }

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(square1_);
        visit(square2_);
        visit(wave_);
        visit(noise_);
        visit(left_volume_);
        visit(right_volume_);
        visit(panning_);
        visit(power_);
        visit(frame_counter_);
        visit(frame_step_);
        visit(sample_counter_);
        visit.block(registers_.data(), registers_.size());
    }

private:
    struct Square {
        bool enabled = false;
        bool dac_on = false;
        uint8_t duty = 2;
        uint8_t duty_step = 0;
        uint16_t frequency = 0;     // 11-bit period selector
        uint16_t timer = 0;
        uint8_t length = 0;
        bool length_enabled = false;
        uint8_t volume = 0;
        uint8_t envelope_initial = 0;
        uint8_t envelope_period = 0;
        uint8_t envelope_timer = 0;
        bool envelope_up = false;
        // Channel 1 only.
        bool sweep_enabled = false;
        bool sweep_negate = false;
        uint8_t sweep_period = 0;
        uint8_t sweep_timer = 0;
        uint8_t sweep_shift = 0;
        uint16_t sweep_shadow = 0;

        void clockTimer();
        void clockLength();
        void clockEnvelope();
        void clockSweep();
        void trigger(bool with_sweep);
        uint8_t output() const;
    };

    struct Wave {
        bool enabled = false;
        bool dac_on = false;
        uint16_t frequency = 0;
        uint16_t timer = 0;
        uint16_t length = 0;
        bool length_enabled = false;
        uint8_t volume_shift = 0;
        uint8_t position = 0;
        uint8_t samples[16] = {};

        void clockTimer();
        void clockLength();
        void trigger();
        uint8_t output() const;
    };

    struct Noise {
        bool enabled = false;
        bool dac_on = false;
        uint16_t shift_register = 0x7FFF;
        bool width_7bit = false;
        uint8_t divisor_code = 0;
        uint8_t clock_shift = 0;
        uint16_t timer = 0;
        uint8_t length = 0;
        bool length_enabled = false;
        uint8_t volume = 0;
        uint8_t envelope_initial = 0;
        uint8_t envelope_period = 0;
        uint8_t envelope_timer = 0;
        bool envelope_up = false;

        void clockTimer();
        void clockLength();
        void clockEnvelope();
        void trigger();
        uint8_t output() const;
    };

    void clockFrameSequencer();
    void powerOff();
    float mix() const;

    std::vector<float> samples_;

    Square square1_;
    Square square2_;
    Wave wave_;
    Noise noise_;

    uint8_t left_volume_ = 7;
    uint8_t right_volume_ = 7;
    uint8_t panning_ = 0xF3;
    bool power_ = true;

    unsigned frame_counter_ = 0;
    unsigned frame_step_ = 0;
    unsigned sample_counter_ = 0;

    std::array<uint8_t, 0x30> registers_{};
};

} // namespace emu::gb
