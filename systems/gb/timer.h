#pragma once

#include <cstdint>

namespace emu::gb {

class Memory;

// DIV (FF04) is the top 8 bits of a 16-bit counter clocked at the CPU rate.
// TIMA follows the falling edge of one selected bit of that counter, which is
// why writing DIV or TAC can tick the timer all by itself.
class Timer {
public:
    explicit Timer(Memory& memory);

    void reset();
    void tick(unsigned cycles);

    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(counter_);
        visit(tima_);
        visit(tma_);
        visit(tac_);
        visit(reload_delay_);
    }

private:
    bool selectedBit() const;
    void detectFallingEdge(bool previous);

    Memory& memory_;

    uint16_t counter_ = 0;
    uint8_t tima_ = 0;
    uint8_t tma_ = 0;
    uint8_t tac_ = 0;
    // Hardware reloads TIMA from TMA four cycles after it overflows, and reads
    // during that window see zero. Timing test ROMs check exactly this.
    uint8_t reload_delay_ = 0;
};

} // namespace emu::gb
