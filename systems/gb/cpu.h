#pragma once

#include "memory.h"
#include "timer.h"

namespace emu::gb {

class CPU {
public:
    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(a_); visit(f_); visit(b_); visit(c_);
        visit(d_); visit(e_); visit(h_); visit(l_);
        visit(pc_); visit(sp_); visit(ime_); visit(halted_);
    }
    CPU(Memory& memory, Timer& timer);

    void reset();
    unsigned step();

    void setInput(uint8_t buttons);
    bool halted() const { return halted_; }

    uint8_t a() const { return a_; }
    uint8_t f() const { return f_; }
    uint8_t b() const { return b_; }
    uint8_t c() const { return c_; }
    uint8_t d() const { return d_; }
    uint8_t e() const { return e_; }
    uint8_t h() const { return h_; }
    uint8_t l() const { return l_; }
    uint16_t pc() const { return pc_; }
    uint16_t sp() const { return sp_; }

private:
    uint8_t fetch8();
    uint16_t fetch16();

    void push16(uint16_t value);
    uint16_t pop16();

    void setFlag(uint8_t mask, bool value);
    bool flag(uint8_t mask) const;

    void executeOpcode(uint8_t opcode);
    void executeCB(uint8_t opcode);

    uint16_t hl() const;
    void setHl(uint16_t value);

    uint8_t readHl() const;
    void writeHl(uint8_t value);

    void inc8(uint8_t& value);
    void dec8(uint8_t& value);

    Memory& memory_;
    Timer& timer_;

    uint8_t a_ = 0;
    uint8_t f_ = 0;
    uint8_t b_ = 0;
    uint8_t c_ = 0;
    uint8_t d_ = 0;
    uint8_t e_ = 0;
    uint8_t h_ = 0;
    uint8_t l_ = 0;

    uint16_t pc_ = 0x0100;
    uint16_t sp_ = 0xFFFE;

    bool ime_ = false;
    bool halted_ = false;

};
}