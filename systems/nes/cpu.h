#pragma once

#include <stdint>
#include <string>

namespace nas
{
class Bus;

class CPU {
public:
    explicit CPU(Bus& bus) : bus_(bus) {}
 
    void reset();
    void irq();    // maskable — ignored when the I flag is set
    void nmi();    // non-maskable, fired by the PPU at the start of vblank
    void clock();  // advance exactly one cycle
 
    bool instructionComplete() const { return cycles_ == 0; }
 
    // Runs until the current instruction retires. Convenience for tests.
    void stepInstruction();
 
    enum Flag : uint8_t {
        C = 1 << 0,  // carry
        Z = 1 << 1,  // zero
        I = 1 << 2,  // interrupt disable
        D = 1 << 3,  // decimal (present but inert on the 2A03)
        B = 1 << 4,  // break — only ever exists on the stack
        U = 1 << 5,  // unused, reads as 1
        V = 1 << 6,  // overflow
        N = 1 << 7,  // negative
    };

    uint8_t  a = 0, x = 0, y = 0, sp = 0, status = 0;
    uint16_t pc = 0;

    uint64_t cycleCount() const { return total_cycles_; }

    std::string trace();

private:
    Bus& bus_;

    uint8_t  read(uint16_t addr);
    void     write(uint16_t addr, uint8_t data);
    uint8_t  peek(uint16_t addr);

    void setFlag(Flag f, bool v) { if (v) status |= f; else status &= static_cast<uint8_t>(~f); }
    bool getFlag(Flag f) const   { return (status & f) != 0; }
 
    void push(uint8_t v);
    uint8_t pop();
 
    uint8_t fetch();  // resolves the operand for the current addressing mode
 
    uint8_t IMP(); uint8_t ACC(); uint8_t IMM(); uint8_t ZP0();
    uint8_t ZPX(); uint8_t ZPY(); uint8_t REL(); uint8_t ABS();
    uint8_t ABX(); uint8_t ABY(); uint8_t IND(); uint8_t IZX(); uint8_t IZY();
 
    uint8_t ADC(); uint8_t AND(); uint8_t ASL(); uint8_t BCC();
    uint8_t BCS(); uint8_t BEQ(); uint8_t BIT(); uint8_t BMI();
    uint8_t BNE(); uint8_t BPL(); uint8_t BRK(); uint8_t BVC();
    uint8_t BVS(); uint8_t CLC(); uint8_t CLD(); uint8_t CLI();
    uint8_t CLV(); uint8_t CMP(); uint8_t CPX(); uint8_t CPY();
    uint8_t DEC(); uint8_t DEX(); uint8_t DEY(); uint8_t EOR();
    uint8_t INC(); uint8_t INX(); uint8_t INY(); uint8_t JMP();
    uint8_t JSR(); uint8_t LDA(); uint8_t LDX(); uint8_t LDY();
    uint8_t LSR(); uint8_t NOP(); uint8_t ORA(); uint8_t PHA();
    uint8_t PHP(); uint8_t PLA(); uint8_t PLP(); uint8_t ROL();
    uint8_t ROR(); uint8_t RTI(); uint8_t RTS(); uint8_t SBC();
    uint8_t SEC(); uint8_t SED(); uint8_t SEI(); uint8_t STA();
    uint8_t STX(); uint8_t STY(); uint8_t TAX(); uint8_t TAY();
    uint8_t TSX(); uint8_t TXA(); uint8_t TXS(); uint8_t TYA();

    uint8_t LAX(); uint8_t SAX(); uint8_t DCP(); uint8_t ISC();
    uint8_t SLO(); uint8_t RLA(); uint8_t SRE(); uint8_t RRA();
    uint8_t ANC(); uint8_t ALR(); uint8_t ARR(); uint8_t AXS();
    uint8_t XXX();

    void branch();

    struct Instructions
    {
        const char* name;
        uint8_t (CPU::*op)();
        uint8_t (CPU::*mode)();
        uint8_t cycles;
    };
    static const Instruction kTable[256];

    uint16_t addr_abs_  = 0;
    uint16_t addr_rel_  = 0;
    uint8_t  fetched_   = 0;
    uint8_t  opcode_    = 0;
    uint8_t  cycles_    = 0;
    bool     acc_mode_  = false;
    uint64_t total_cycles_ = 0;
    
};

} // namespace name
