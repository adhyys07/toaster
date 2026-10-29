#pragma once
#include <cstdint>

namespace nes {

class APU{
public:
    APU() { reset(); }
    void reset();
    void clock();
    void cpuWrite(uint16_t addr, uint8_t data);
    uint8_t cpuRead(uint16_t addr);
 
    float output() const;
    bool irq = false;

private:
    struct Envelope {
        bool    start    = false;
        bool    loop     = false;
        bool    constant = false;
        uint8_t volume   = 0;  
        uint8_t decay    = 0;
        uint8_t divider  = 0;
 
        void clock();
        uint8_t level() const { return constant ? volume : decay; }
    };
    struct Sweep {
        bool    enabled = false;
        bool    negate  = false;
        bool    reload  = false;
        uint8_t shift   = 0;
        uint8_t period  = 0;
        uint8_t divider = 0;
    };
 
    struct Pulse {
        bool     enabled      = false;
        uint8_t  duty         = 0;
        uint8_t  duty_pos     = 0;
        uint16_t timer        = 0;
        uint16_t timer_period = 0;
        uint8_t  length       = 0;
        bool     length_halt  = false;
        bool     ones_comp    = false;
        Envelope env;
        Sweep    sweep;
 
        void clockTimer();
        void clockSweep();
        uint16_t targetPeriod() const;
        bool muted() const;
        uint8_t level() const;
    };
 
    struct Triangle {
        bool     enabled        = false;
        uint16_t timer          = 0;
        uint16_t timer_period   = 0;
        uint8_t  length         = 0;
        bool     length_halt    = false;
        uint8_t  linear         = 0;
        uint8_t  linear_reload  = 0;
        bool     linear_reload_flag = false;
        uint8_t  seq_pos        = 0;
 
        void clockTimer();
        uint8_t level() const;
    };
 
    struct Noise {
        bool     enabled      = false;
        bool     mode         = false; 
        uint16_t shift_reg    = 1;
        uint16_t timer        = 0;
        uint16_t timer_period = 0;
        uint8_t  length       = 0;
        bool     length_halt  = false;
        Envelope env;
 
        void clockTimer();
        uint8_t level() const;
    };
 
    void clockQuarterFrame(); 
    void clockHalfFrame();     
 
    Pulse    pulse1_, pulse2_;
    Triangle triangle_;
    Noise    noise_;
 
    uint32_t frame_cycle_   = 0;
    bool     five_step_     = false;
    bool     irq_inhibit_   = false;
    bool     cycle_parity_  = false; 
 
    static const uint8_t  kLengthTable[32];
    static const uint8_t  kDutyTable[4];
    static const uint16_t kNoisePeriod[16];
    static const uint8_t  kTriangleSeq[32];
};
 
}  // namespace nes
 