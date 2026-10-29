#include "apu.h"

namespace nes {

const uint8_t APU::kLengthTable[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

const uint8_t APU::kDutyTable[4] = { 0x40, 0x60, 0x78, 0x9F };

const uint16_t APU::kNoisePeriod[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

const uint8_t APU::kTriangleSeq[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
     0,  1,  2,  3,  4,  5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};


void APU::Envelope::clock() {
    if (start) {
        start   = false;
        decay   = 15;
        divider = volume;
        return;
    }
    if (divider > 0) {
        divider--;
        return;
    }
    divider = volume;
    if (decay > 0)   decay--;
    else if (loop)   decay = 15;
}


void APU::Pulse::clockTimer() {
    if (timer == 0) {
        timer    = timer_period;
        duty_pos = static_cast<uint8_t>((duty_pos + 1) & 0x07);
    } else {
        timer--;
    }
}

uint16_t APU::Pulse::targetPeriod() const {
    const uint16_t delta = static_cast<uint16_t>(timer_period >> sweep.shift);
    if (!sweep.negate) return static_cast<uint16_t>(timer_period + delta);
    const int t = static_cast<int>(timer_period) - delta - (ones_comp ? 1 : 0);
    return static_cast<uint16_t>(t < 0 ? 0 : t);
}

void APU::Pulse::clockSweep() {
    if (sweep.divider == 0 && sweep.enabled && sweep.shift > 0 && !muted()) {
        timer_period = targetPeriod();
    }
    if (sweep.divider == 0 || sweep.reload) {
        sweep.divider = sweep.period;
        sweep.reload  = false;
    } else {
        sweep.divider--;
    }
}

bool APU::Pulse::muted() const {
    return timer_period < 8 || targetPeriod() > 0x07FF;
}

uint8_t APU::Pulse::level() const {
    if (!enabled || length == 0 || muted()) return 0;
    const uint8_t pattern = kDutyTable[duty];
    return ((pattern >> (7 - duty_pos)) & 1) ? env.level() : 0;
}


void APU::Triangle::clockTimer() {
    if (timer == 0) {
        timer = timer_period;
        if (length > 0 && linear > 0) {
            seq_pos = static_cast<uint8_t>((seq_pos + 1) & 0x1F);
        }
    } else {
        timer--;
    }
}

uint8_t APU::Triangle::level() const {
    if (!enabled) return 0;
    if (timer_period < 2) return 0;
    return kTriangleSeq[seq_pos];
}


void APU::Noise::clockTimer() {
    if (timer == 0) {
        timer = timer_period;
        const uint16_t bit  = mode ? 6 : 1;
        const uint16_t feed = (shift_reg & 1) ^ ((shift_reg >> bit) & 1);
        shift_reg = static_cast<uint16_t>((shift_reg >> 1) | (feed << 14));
    } else {
        timer--;
    }
}

uint8_t APU::Noise::level() const {
    if (!enabled || length == 0 || (shift_reg & 1)) return 0;
    return env.level();
}


void APU::reset() {
    pulse1_ = Pulse{};
    pulse2_ = Pulse{};
    pulse1_.ones_comp = true;
    triangle_ = Triangle{};
    noise_    = Noise{};
    noise_.shift_reg = 1;
    frame_cycle_  = 0;
    five_step_    = false;
    irq_inhibit_  = false;
    cycle_parity_ = false;
    irq = false;
}

void APU::clockQuarterFrame() {
    pulse1_.env.clock();
    pulse2_.env.clock();
    noise_.env.clock();

    if (triangle_.linear_reload_flag) {
        triangle_.linear = triangle_.linear_reload;
    } else if (triangle_.linear > 0) {
        triangle_.linear--;
    }
    if (!triangle_.length_halt) triangle_.linear_reload_flag = false;
}

void APU::clockHalfFrame() {
    if (!pulse1_.length_halt   && pulse1_.length   > 0) pulse1_.length--;
    if (!pulse2_.length_halt   && pulse2_.length   > 0) pulse2_.length--;
    if (!triangle_.length_halt && triangle_.length > 0) triangle_.length--;
    if (!noise_.length_halt    && noise_.length    > 0) noise_.length--;

    pulse1_.clockSweep();
    pulse2_.clockSweep();
}

void APU::clock() {
    triangle_.clockTimer();

    cycle_parity_ = !cycle_parity_;
    if (cycle_parity_) {
        pulse1_.clockTimer();
        pulse2_.clockTimer();
        noise_.clockTimer();
    }

    frame_cycle_++;

    if (!five_step_) {
        switch (frame_cycle_) {
        case 3729:  clockQuarterFrame(); break;
        case 7457:  clockQuarterFrame(); clockHalfFrame(); break;
        case 11186: clockQuarterFrame(); break;
        case 14915:
            clockQuarterFrame();
            clockHalfFrame();
            if (!irq_inhibit_) irq = true;
            frame_cycle_ = 0;
            break;
        default: break;
        }
    } else {
        switch (frame_cycle_) {
        case 3729:  clockQuarterFrame(); break;
        case 7457:  clockQuarterFrame(); clockHalfFrame(); break;
        case 11186: clockQuarterFrame(); break;
        case 18641:
            clockQuarterFrame();
            clockHalfFrame();
            frame_cycle_ = 0;
            break;
        default: break;
        }
    }
}

float APU::output() const {
    const float pulse_out = 0.00752f * (pulse1_.level() + pulse2_.level());
    const float tnd_out   = 0.00851f * triangle_.level() + 0.00494f * noise_.level();
    return pulse_out + tnd_out;
}

uint8_t APU::cpuRead(uint16_t addr) {
    if (addr != 0x4015) return 0;

    uint8_t status = 0;
    if (pulse1_.length   > 0) status |= 0x01;
    if (pulse2_.length   > 0) status |= 0x02;
    if (triangle_.length > 0) status |= 0x04;
    if (noise_.length    > 0) status |= 0x08;
    if (irq)                  status |= 0x40;
    irq = false;  
    return status;
}

void APU::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr) {
    case 0x4000:
        pulse1_.duty         = (data >> 6) & 0x03;
        pulse1_.length_halt  = (data & 0x20) != 0;
        pulse1_.env.loop     = pulse1_.length_halt;
        pulse1_.env.constant = (data & 0x10) != 0;
        pulse1_.env.volume   = data & 0x0F;
        break;
    case 0x4001:
        pulse1_.sweep.enabled = (data & 0x80) != 0;
        pulse1_.sweep.period  = (data >> 4) & 0x07;
        pulse1_.sweep.negate  = (data & 0x08) != 0;
        pulse1_.sweep.shift   = data & 0x07;
        pulse1_.sweep.reload  = true;
        break;
    case 0x4002:
        pulse1_.timer_period = static_cast<uint16_t>((pulse1_.timer_period & 0x0700) | data);
        break;
    case 0x4003:
        pulse1_.timer_period = static_cast<uint16_t>((pulse1_.timer_period & 0x00FF) |
                                                     ((data & 0x07) << 8));
        if (pulse1_.enabled) pulse1_.length = kLengthTable[(data >> 3) & 0x1F];
        pulse1_.duty_pos  = 0;
        pulse1_.env.start = true;
        break;

    case 0x4004:
        pulse2_.duty         = (data >> 6) & 0x03;
        pulse2_.length_halt  = (data & 0x20) != 0;
        pulse2_.env.loop     = pulse2_.length_halt;
        pulse2_.env.constant = (data & 0x10) != 0;
        pulse2_.env.volume   = data & 0x0F;
        break;
    case 0x4005:
        pulse2_.sweep.enabled = (data & 0x80) != 0;
        pulse2_.sweep.period  = (data >> 4) & 0x07;
        pulse2_.sweep.negate  = (data & 0x08) != 0;
        pulse2_.sweep.shift   = data & 0x07;
        pulse2_.sweep.reload  = true;
        break;
    case 0x4006:
        pulse2_.timer_period = static_cast<uint16_t>((pulse2_.timer_period & 0x0700) | data);
        break;
    case 0x4007:
        pulse2_.timer_period = static_cast<uint16_t>((pulse2_.timer_period & 0x00FF) |
                                                     ((data & 0x07) << 8));
        if (pulse2_.enabled) pulse2_.length = kLengthTable[(data >> 3) & 0x1F];
        pulse2_.duty_pos  = 0;
        pulse2_.env.start = true;
        break;

    case 0x4008:
        triangle_.length_halt   = (data & 0x80) != 0;
        triangle_.linear_reload = data & 0x7F;
        break;
    case 0x400A:
        triangle_.timer_period = static_cast<uint16_t>((triangle_.timer_period & 0x0700) | data);
        break;
    case 0x400B:
        triangle_.timer_period = static_cast<uint16_t>((triangle_.timer_period & 0x00FF) |
                                                       ((data & 0x07) << 8));
        if (triangle_.enabled) triangle_.length = kLengthTable[(data >> 3) & 0x1F];
        triangle_.linear_reload_flag = true;
        break;

    case 0x400C:
        noise_.length_halt  = (data & 0x20) != 0;
        noise_.env.loop     = noise_.length_halt;
        noise_.env.constant = (data & 0x10) != 0;
        noise_.env.volume   = data & 0x0F;
        break;
    case 0x400E:
        noise_.mode         = (data & 0x80) != 0;
        noise_.timer_period = kNoisePeriod[data & 0x0F];
        break;
    case 0x400F:
        if (noise_.enabled) noise_.length = kLengthTable[(data >> 3) & 0x1F];
        noise_.env.start = true;
        break;

    case 0x4015:
        pulse1_.enabled   = (data & 0x01) != 0;
        pulse2_.enabled   = (data & 0x02) != 0;
        triangle_.enabled = (data & 0x04) != 0;
        noise_.enabled    = (data & 0x08) != 0;
        if (!pulse1_.enabled)   pulse1_.length   = 0;
        if (!pulse2_.enabled)   pulse2_.length   = 0;
        if (!triangle_.enabled) triangle_.length = 0;
        if (!noise_.enabled)    noise_.length    = 0;
        break;

    case 0x4017:
        five_step_   = (data & 0x80) != 0;
        irq_inhibit_ = (data & 0x40) != 0;
        if (irq_inhibit_) irq = false;
        frame_cycle_ = 0;
        if (five_step_) { 
            clockQuarterFrame();
            clockHalfFrame();
        }
        break;

    default:
        break;
    }
}

}  // namespace nes