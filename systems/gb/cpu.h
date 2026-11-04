#pragma once

#include <cstdint>

namespace emu::gb {

class APU;
class Memory;
class PPU;
class Timer;

// The LR35902. Decoding is structural rather than a 512-entry switch, because
// the instruction set is regular: the register, ALU operation and condition
// are all fields inside the opcode. Timing falls out of counting bus accesses,
// so there is no separate cycle table that could drift from the code.
class CPU {
public:
    CPU(Memory& memory, Timer& timer, PPU& ppu, APU& apu);

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

    template <typename Visitor>
    void visitState(Visitor& visit) {
        visit(a_); visit(f_); visit(b_); visit(c_);
        visit(d_); visit(e_); visit(h_); visit(l_);
        visit(pc_); visit(sp_);
        visit(ime_);
        visit(ime_pending_);
        visit(halted_);
        visit(halt_bug_);
        visit(stopped_);
    }

private:
    // Every bus access costs four T-cycles and advances the peripherals, so
    // the video and audio units see the same interleaving the hardware does.
    void tick();

    uint8_t read8(uint16_t address);
    void write8(uint16_t address, uint8_t value);
    uint8_t fetch8();
    uint16_t fetch16();

    void push16(uint16_t value);
    uint16_t pop16();

    void setFlag(uint8_t mask, bool value);
    bool flag(uint8_t mask) const;

    uint16_t hl() const;
    void setHl(uint16_t value);

    // Opcode field decoders. Register index 0..7 is B,C,D,E,H,L,(HL),A and
    // pair index 0..3 is BC,DE,HL,SP.
    uint8_t readReg(int index);
    void writeReg(int index, uint8_t value);
    uint16_t readPair(int index) const;
    void writePair(int index, uint16_t value);
    bool condition(int index) const;

    void alu(int operation, uint8_t operand);
    uint8_t rotate(int operation, uint8_t value);
    void inc8(uint8_t& value);
    void dec8(uint8_t& value);
    void daa();
    void addToHl(uint16_t operand);

    unsigned serviceInterrupts();
    void executeOpcode(uint8_t opcode);
    void executeCB(uint8_t opcode);

    Memory& memory_;
    Timer& timer_;
    PPU& ppu_;
    APU& apu_;

    uint8_t a_ = 0x01;
    uint8_t f_ = 0xB0;
    uint8_t b_ = 0x00;
    uint8_t c_ = 0x13;
    uint8_t d_ = 0x00;
    uint8_t e_ = 0xD8;
    uint8_t h_ = 0x01;
    uint8_t l_ = 0x4D;

    uint16_t pc_ = 0x0100;
    uint16_t sp_ = 0xFFFE;

    bool ime_ = false;
    // EI takes effect only after the instruction that follows it.
    bool ime_pending_ = false;
    bool halted_ = false;
    // HALT with interrupts pending but IME clear does not halt, and the next
    // opcode byte is read twice.
    bool halt_bug_ = false;
    bool stopped_ = false;

    unsigned cycles_ = 0;
};

} // namespace emu::gb
