#include "cpu.h"
#include "bus.h"

#include <cstdio>

namespace nes {

#define OP(n, m, c) { #n, &CPU::n, &CPU::m, c }

const CPU::Instruction CPU::kTable[256] = {
       OP(BRK,IMP,7), OP(ORA,IZX,6), OP(XXX,IMP,2), OP(SLO,IZX,8),
       OP(NOP,ZP0,3), OP(ORA,ZP0,3), OP(ASL,ZP0,5), OP(SLO,ZP0,5),
       OP(PHP,IMP,3), OP(ORA,IMM,2), OP(ASL,ACC,2), OP(ANC,IMM,2),
       OP(NOP,ABS,4), OP(ORA,ABS,4), OP(ASL,ABS,6), OP(SLO,ABS,6),
       OP(BPL,REL,2), OP(ORA,IZY,5), OP(XXX,IMP,2), OP(SLO,IZY,8),
       OP(NOP,ZPX,4), OP(ORA,ZPX,4), OP(ASL,ZPX,6), OP(SLO,ZPX,6),
       OP(CLC,IMP,2), OP(ORA,ABY,4), OP(NOP,IMP,2), OP(SLO,ABY,7),
       OP(NOP,ABX,4), OP(ORA,ABX,4), OP(ASL,ABX,7), OP(SLO,ABX,7),
       OP(JSR,ABS,6), OP(AND,IZX,6), OP(XXX,IMP,2), OP(RLA,IZX,8),
       OP(BIT,ZP0,3), OP(AND,ZP0,3), OP(ROL,ZP0,5), OP(RLA,ZP0,5),
       OP(PLP,IMP,4), OP(AND,IMM,2), OP(ROL,ACC,2), OP(ANC,IMM,2),
       OP(BIT,ABS,4), OP(AND,ABS,4), OP(ROL,ABS,6), OP(RLA,ABS,6),
       OP(BMI,REL,2), OP(AND,IZY,5), OP(XXX,IMP,2), OP(RLA,IZY,8),
       OP(NOP,ZPX,4), OP(AND,ZPX,4), OP(ROL,ZPX,6), OP(RLA,ZPX,6),
       OP(SEC,IMP,2), OP(AND,ABY,4), OP(NOP,IMP,2), OP(RLA,ABY,7),
       OP(NOP,ABX,4), OP(AND,ABX,4), OP(ROL,ABX,7), OP(RLA,ABX,7),
       OP(RTI,IMP,6), OP(EOR,IZX,6), OP(XXX,IMP,2), OP(SRE,IZX,8),
       OP(NOP,ZP0,3), OP(EOR,ZP0,3), OP(LSR,ZP0,5), OP(SRE,ZP0,5),
       OP(PHA,IMP,3), OP(EOR,IMM,2), OP(LSR,ACC,2), OP(ALR,IMM,2),
       OP(JMP,ABS,3), OP(EOR,ABS,4), OP(LSR,ABS,6), OP(SRE,ABS,6),
       OP(BVC,REL,2), OP(EOR,IZY,5), OP(XXX,IMP,2), OP(SRE,IZY,8),
       OP(NOP,ZPX,4), OP(EOR,ZPX,4), OP(LSR,ZPX,6), OP(SRE,ZPX,6),
       OP(CLI,IMP,2), OP(EOR,ABY,4), OP(NOP,IMP,2), OP(SRE,ABY,7),
       OP(NOP,ABX,4), OP(EOR,ABX,4), OP(LSR,ABX,7), OP(SRE,ABX,7),
       OP(RTS,IMP,6), OP(ADC,IZX,6), OP(XXX,IMP,2), OP(RRA,IZX,8),
       OP(NOP,ZP0,3), OP(ADC,ZP0,3), OP(ROR,ZP0,5), OP(RRA,ZP0,5),
       OP(PLA,IMP,4), OP(ADC,IMM,2), OP(ROR,ACC,2), OP(ARR,IMM,2),
       OP(JMP,IND,5), OP(ADC,ABS,4), OP(ROR,ABS,6), OP(RRA,ABS,6),
       OP(BVS,REL,2), OP(ADC,IZY,5), OP(XXX,IMP,2), OP(RRA,IZY,8),
       OP(NOP,ZPX,4), OP(ADC,ZPX,4), OP(ROR,ZPX,6), OP(RRA,ZPX,6),
       OP(SEI,IMP,2), OP(ADC,ABY,4), OP(NOP,IMP,2), OP(RRA,ABY,7),
       OP(NOP,ABX,4), OP(ADC,ABX,4), OP(ROR,ABX,7), OP(RRA,ABX,7),
       OP(NOP,IMM,2), OP(STA,IZX,6), OP(NOP,IMM,2), OP(SAX,IZX,6),
       OP(STY,ZP0,3), OP(STA,ZP0,3), OP(STX,ZP0,3), OP(SAX,ZP0,3),
       OP(DEY,IMP,2), OP(NOP,IMM,2), OP(TXA,IMP,2), OP(XXX,IMM,2),
       OP(STY,ABS,4), OP(STA,ABS,4), OP(STX,ABS,4), OP(SAX,ABS,4),
       OP(BCC,REL,2), OP(STA,IZY,6), OP(XXX,IMP,2), OP(XXX,IZY,6),
       OP(STY,ZPX,4), OP(STA,ZPX,4), OP(STX,ZPY,4), OP(SAX,ZPY,4),
       OP(TYA,IMP,2), OP(STA,ABY,5), OP(TXS,IMP,2), OP(XXX,ABY,5),
       OP(XXX,ABX,5), OP(STA,ABX,5), OP(XXX,ABY,5), OP(XXX,ABY,5),
       OP(LDY,IMM,2), OP(LDA,IZX,6), OP(LDX,IMM,2), OP(LAX,IZX,6),
       OP(LDY,ZP0,3), OP(LDA,ZP0,3), OP(LDX,ZP0,3), OP(LAX,ZP0,3),
       OP(TAY,IMP,2), OP(LDA,IMM,2), OP(TAX,IMP,2), OP(LAX,IMM,2),
       OP(LDY,ABS,4), OP(LDA,ABS,4), OP(LDX,ABS,4), OP(LAX,ABS,4),
       OP(BCS,REL,2), OP(LDA,IZY,5), OP(XXX,IMP,2), OP(LAX,IZY,5),
       OP(LDY,ZPX,4), OP(LDA,ZPX,4), OP(LDX,ZPY,4), OP(LAX,ZPY,4),
       OP(CLV,IMP,2), OP(LDA,ABY,4), OP(TSX,IMP,2), OP(XXX,ABY,4),
       OP(LDY,ABX,4), OP(LDA,ABX,4), OP(LDX,ABY,4), OP(LAX,ABY,4),
       OP(CPY,IMM,2), OP(CMP,IZX,6), OP(NOP,IMM,2), OP(DCP,IZX,8),
       OP(CPY,ZP0,3), OP(CMP,ZP0,3), OP(DEC,ZP0,5), OP(DCP,ZP0,5),
       OP(INY,IMP,2), OP(CMP,IMM,2), OP(DEX,IMP,2), OP(AXS,IMM,2),
       OP(CPY,ABS,4), OP(CMP,ABS,4), OP(DEC,ABS,6), OP(DCP,ABS,6),
       OP(BNE,REL,2), OP(CMP,IZY,5), OP(XXX,IMP,2), OP(DCP,IZY,8),
       OP(NOP,ZPX,4), OP(CMP,ZPX,4), OP(DEC,ZPX,6), OP(DCP,ZPX,6),
       OP(CLD,IMP,2), OP(CMP,ABY,4), OP(NOP,IMP,2), OP(DCP,ABY,7),
       OP(NOP,ABX,4), OP(CMP,ABX,4), OP(DEC,ABX,7), OP(DCP,ABX,7),
       OP(CPX,IMM,2), OP(SBC,IZX,6), OP(NOP,IMM,2), OP(ISC,IZX,8),
       OP(CPX,ZP0,3), OP(SBC,ZP0,3), OP(INC,ZP0,5), OP(ISC,ZP0,5),
       OP(INX,IMP,2), OP(SBC,IMM,2), OP(NOP,IMP,2), OP(SBC,IMM,2),
       OP(CPX,ABS,4), OP(SBC,ABS,4), OP(INC,ABS,6), OP(ISC,ABS,6),
       OP(BEQ,REL,2), OP(SBC,IZY,5), OP(XXX,IMP,2), OP(ISC,IZY,8),
       OP(NOP,ZPX,4), OP(SBC,ZPX,4), OP(INC,ZPX,6), OP(ISC,ZPX,6),
       OP(SED,IMP,2), OP(SBC,ABY,4), OP(NOP,IMP,2), OP(ISC,ABY,7),
       OP(NOP,ABX,4), OP(SBC,ABX,4), OP(INC,ABX,7), OP(ISC,ABX,7),
};

#undef OP


uint8_t CPU::read(uint16_t addr)              { return bus_.cpuRead(addr); }
void    CPU::write(uint16_t addr, uint8_t d)  { bus_.cpuWrite(addr, d); }
uint8_t CPU::peek(uint16_t addr)              { return bus_.cpuRead(addr, true); }

void CPU::push(uint8_t v) {
    write(0x0100 + sp, v);
    sp--;
}

uint8_t CPU::pop() {
    sp++;
    return read(0x0100 + sp);
}

uint8_t CPU::fetch() {
    fetched_ = acc_mode_ ? a : read(addr_abs_);
    return fetched_;
}

void CPU::reset() {
    a = x = y = 0;
    sp = 0xFD;
    status = U | I;

    const uint16_t lo = read(0xFFFC);
    const uint16_t hi = read(0xFFFD);
    pc = static_cast<uint16_t>((hi << 8) | lo);

    addr_abs_ = addr_rel_ = 0;
    fetched_  = 0;
    acc_mode_ = false;
    cycles_   = 0;
}

void CPU::nmi() {
    push(static_cast<uint8_t>((pc >> 8) & 0xFF));
    push(static_cast<uint8_t>(pc & 0xFF));
    push(static_cast<uint8_t>((status & ~B) | U));
    setFlag(I, true);

    const uint16_t lo = read(0xFFFA);
    const uint16_t hi = read(0xFFFB);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    cycles_ = 8;
}

void CPU::irq() {
    if (getFlag(I)) return;

    push(static_cast<uint8_t>((pc >> 8) & 0xFF));
    push(static_cast<uint8_t>(pc & 0xFF));
    push(static_cast<uint8_t>((status & ~B) | U));
    setFlag(I, true);

    const uint16_t lo = read(0xFFFE);
    const uint16_t hi = read(0xFFFF);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    cycles_ = 7;
}

void CPU::clock() {
    if (cycles_ == 0) {
        opcode_ = read(pc++);
        setFlag(U, true);

        const Instruction& ins = kTable[opcode_];
        cycles_   = ins.cycles;
        acc_mode_ = false;

        const uint8_t mode_extra = (this->*ins.mode)();
        const uint8_t op_extra   = (this->*ins.op)();
        cycles_ = static_cast<uint8_t>(cycles_ + (mode_extra & op_extra));

        setFlag(U, true);
    }
    cycles_--;
    total_cycles_++;
}

void CPU::stepInstruction() {
    do { clock(); } while (cycles_ != 0);
}


uint8_t CPU::IMP() { return 0; }

uint8_t CPU::ACC() { acc_mode_ = true; return 0; }

uint8_t CPU::IMM() { addr_abs_ = pc++; return 0; }

uint8_t CPU::ZP0() { addr_abs_ = read(pc++) & 0x00FF; return 0; }

uint8_t CPU::ZPX() { addr_abs_ = (read(pc++) + x) & 0x00FF; return 0; }

uint8_t CPU::ZPY() { addr_abs_ = (read(pc++) + y) & 0x00FF; return 0; }

uint8_t CPU::REL() {
    addr_rel_ = read(pc++);
    if (addr_rel_ & 0x80) addr_rel_ |= 0xFF00;  
    return 0;
}

uint8_t CPU::ABS() {
    const uint16_t lo = read(pc++);
    const uint16_t hi = read(pc++);
    addr_abs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::ABX() {
    const uint16_t lo = read(pc++);
    const uint16_t hi = read(pc++);
    addr_abs_ = static_cast<uint16_t>(((hi << 8) | lo) + x);
    return ((addr_abs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t CPU::ABY() {
    const uint16_t lo = read(pc++);
    const uint16_t hi = read(pc++);
    addr_abs_ = static_cast<uint16_t>(((hi << 8) | lo) + y);
    return ((addr_abs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t CPU::IND() {
    const uint16_t lo  = read(pc++);
    const uint16_t hi  = read(pc++);
    const uint16_t ptr = static_cast<uint16_t>((hi << 8) | lo);

    if (lo == 0x00FF) {
        addr_abs_ = static_cast<uint16_t>((read(ptr & 0xFF00) << 8) | read(ptr));
    } else {
        addr_abs_ = static_cast<uint16_t>((read(ptr + 1) << 8) | read(ptr));
    }
    return 0;
}

uint8_t CPU::IZX() {
    const uint16_t t  = read(pc++);
    const uint16_t lo = read((t + x) & 0x00FF);
    const uint16_t hi = read((t + x + 1) & 0x00FF);
    addr_abs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::IZY() {
    const uint16_t t  = read(pc++);
    const uint16_t lo = read(t & 0x00FF);
    const uint16_t hi = read((t + 1) & 0x00FF);
    addr_abs_ = static_cast<uint16_t>(((hi << 8) | lo) + y);
    return ((addr_abs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}


uint8_t CPU::ADC() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(a) + fetched_ + (getFlag(C) ? 1 : 0);
    setFlag(C, tmp > 0xFF);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(V, (~(static_cast<uint16_t>(a) ^ fetched_) &
                 (static_cast<uint16_t>(a) ^ tmp)) & 0x0080);
    setFlag(N, tmp & 0x80);
    a = static_cast<uint8_t>(tmp);
    return 1;
}

uint8_t CPU::SBC() {
    fetch();
    const uint16_t value = static_cast<uint16_t>(fetched_) ^ 0x00FF;
    const uint16_t tmp   = static_cast<uint16_t>(a) + value + (getFlag(C) ? 1 : 0);
    setFlag(C, (tmp & 0xFF00) != 0);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(V, (tmp ^ static_cast<uint16_t>(a)) & (tmp ^ value) & 0x0080);
    setFlag(N, tmp & 0x80);
    a = static_cast<uint8_t>(tmp);
    return 1;
}

uint8_t CPU::AND() {
    fetch();
    a &= fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 1;
}

uint8_t CPU::ORA() {
    fetch();
    a |= fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 1;
}

uint8_t CPU::EOR() {
    fetch();
    a ^= fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 1;
}

uint8_t CPU::ASL() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, (tmp & 0xFF00) != 0);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(N, tmp & 0x80);
    if (acc_mode_) a = static_cast<uint8_t>(tmp);
    else           write(addr_abs_, static_cast<uint8_t>(tmp));
    return 0;
}

uint8_t CPU::LSR() {
    fetch();
    setFlag(C, fetched_ & 0x01);
    const uint8_t tmp = static_cast<uint8_t>(fetched_ >> 1);
    setFlag(Z, tmp == 0);
    setFlag(N, tmp & 0x80);
    if (acc_mode_) a = tmp;
    else           write(addr_abs_, tmp);
    return 0;
}

uint8_t CPU::ROL() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(fetched_ << 1) | (getFlag(C) ? 1 : 0);
    setFlag(C, (tmp & 0xFF00) != 0);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(N, tmp & 0x80);
    if (acc_mode_) a = static_cast<uint8_t>(tmp);
    else           write(addr_abs_, static_cast<uint8_t>(tmp));
    return 0;
}

uint8_t CPU::ROR() {
    fetch();
    const uint8_t tmp = static_cast<uint8_t>((getFlag(C) ? 0x80 : 0x00) | (fetched_ >> 1));
    setFlag(C, fetched_ & 0x01);
    setFlag(Z, tmp == 0);
    setFlag(N, tmp & 0x80);
    if (acc_mode_) a = tmp;
    else           write(addr_abs_, tmp);
    return 0;
}

uint8_t CPU::BIT() {
    fetch();
    setFlag(Z, (a & fetched_) == 0);
    setFlag(N, fetched_ & 0x80); 
    setFlag(V, fetched_ & 0x40);
    return 0;
}

uint8_t CPU::CMP() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(a) - fetched_;
    setFlag(C, a >= fetched_);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(N, tmp & 0x80);
    return 1;
}

uint8_t CPU::CPX() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(x) - fetched_;
    setFlag(C, x >= fetched_);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU::CPY() {
    fetch();
    const uint16_t tmp = static_cast<uint16_t>(y) - fetched_;
    setFlag(C, y >= fetched_);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU::DEC() {
    fetch();
    const uint8_t tmp = static_cast<uint8_t>(fetched_ - 1);
    write(addr_abs_, tmp);
    setFlag(Z, tmp == 0);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU::INC() {
    fetch();
    const uint8_t tmp = static_cast<uint8_t>(fetched_ + 1);
    write(addr_abs_, tmp);
    setFlag(Z, tmp == 0);
    setFlag(N, tmp & 0x80);
    return 0;
}

uint8_t CPU::DEX() { x--; setFlag(Z, x == 0); setFlag(N, x & 0x80); return 0; }
uint8_t CPU::DEY() { y--; setFlag(Z, y == 0); setFlag(N, y & 0x80); return 0; }
uint8_t CPU::INX() { x++; setFlag(Z, x == 0); setFlag(N, x & 0x80); return 0; }
uint8_t CPU::INY() { y++; setFlag(Z, y == 0); setFlag(N, y & 0x80); return 0; }

uint8_t CPU::LDA() {
    fetch();
    a = fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 1;
}

uint8_t CPU::LDX() {
    fetch();
    x = fetched_;
    setFlag(Z, x == 0);
    setFlag(N, x & 0x80);
    return 1;
}

uint8_t CPU::LDY() {
    fetch();
    y = fetched_;
    setFlag(Z, y == 0);
    setFlag(N, y & 0x80);
    return 1;
}

uint8_t CPU::STA() { write(addr_abs_, a); return 0; }
uint8_t CPU::STX() { write(addr_abs_, x); return 0; }
uint8_t CPU::STY() { write(addr_abs_, y); return 0; }

uint8_t CPU::TAX() { x = a;  setFlag(Z, x == 0); setFlag(N, x & 0x80); return 0; }
uint8_t CPU::TAY() { y = a;  setFlag(Z, y == 0); setFlag(N, y & 0x80); return 0; }
uint8_t CPU::TXA() { a = x;  setFlag(Z, a == 0); setFlag(N, a & 0x80); return 0; }
uint8_t CPU::TYA() { a = y;  setFlag(Z, a == 0); setFlag(N, a & 0x80); return 0; }
uint8_t CPU::TSX() { x = sp; setFlag(Z, x == 0); setFlag(N, x & 0x80); return 0; }
uint8_t CPU::TXS() { sp = x; return 0; }  // the only transfer that sets no flags

uint8_t CPU::PHA() { push(a); return 0; }

uint8_t CPU::PHP() {
    push(static_cast<uint8_t>(status | B | U));
    return 0;
}

uint8_t CPU::PLA() {
    a = pop();
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 0;
}

uint8_t CPU::PLP() {
    status = static_cast<uint8_t>((pop() & ~B) | U);
    return 0;
}

uint8_t CPU::CLC() { setFlag(C, false); return 0; }
uint8_t CPU::CLD() { setFlag(D, false); return 0; }
uint8_t CPU::CLI() { setFlag(I, false); return 0; }
uint8_t CPU::CLV() { setFlag(V, false); return 0; }
uint8_t CPU::SEC() { setFlag(C, true);  return 0; }
uint8_t CPU::SED() { setFlag(D, true);  return 0; }
uint8_t CPU::SEI() { setFlag(I, true);  return 0; }

uint8_t CPU::JMP() { pc = addr_abs_; return 0; }

uint8_t CPU::JSR() {
    pc--;
    push(static_cast<uint8_t>((pc >> 8) & 0xFF));
    push(static_cast<uint8_t>(pc & 0xFF));
    pc = addr_abs_;
    return 0;
}

uint8_t CPU::RTS() {
    const uint16_t lo = pop();
    const uint16_t hi = pop();
    pc = static_cast<uint16_t>(((hi << 8) | lo) + 1);
    return 0;
}

uint8_t CPU::RTI() {
    status = static_cast<uint8_t>((pop() & ~B) | U);
    const uint16_t lo = pop();
    const uint16_t hi = pop();
    pc = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t CPU::BRK() {
    pc++; 
    push(static_cast<uint8_t>((pc >> 8) & 0xFF));
    push(static_cast<uint8_t>(pc & 0xFF));
    push(static_cast<uint8_t>(status | B | U));
    setFlag(I, true);

    const uint16_t lo = read(0xFFFE);
    const uint16_t hi = read(0xFFFF);
    pc = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

void CPU::branch() {
    cycles_++;
    const uint16_t target = static_cast<uint16_t>(pc + addr_rel_);
    if ((target & 0xFF00) != (pc & 0xFF00)) cycles_++;
    pc = target;
}

uint8_t CPU::BCC() { if (!getFlag(C)) branch(); return 0; }
uint8_t CPU::BCS() { if ( getFlag(C)) branch(); return 0; }
uint8_t CPU::BEQ() { if ( getFlag(Z)) branch(); return 0; }
uint8_t CPU::BNE() { if (!getFlag(Z)) branch(); return 0; }
uint8_t CPU::BMI() { if ( getFlag(N)) branch(); return 0; }
uint8_t CPU::BPL() { if (!getFlag(N)) branch(); return 0; }
uint8_t CPU::BVC() { if (!getFlag(V)) branch(); return 0; }
uint8_t CPU::BVS() { if ( getFlag(V)) branch(); return 0; }

uint8_t CPU::NOP() {
    switch (opcode_) {
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        return 1;
    default:
        return 0;
    }
}


uint8_t CPU::LAX() { 
    fetch();
    a = fetched_;
    x = fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 1;
}

uint8_t CPU::SAX() {  // store A AND X, touching no flags
    write(addr_abs_, static_cast<uint8_t>(a & x));
    return 0;
}

uint8_t CPU::DCP() {  
    fetch();
    const uint8_t tmp = static_cast<uint8_t>(fetched_ - 1);
    write(addr_abs_, tmp);
    setFlag(C, a >= tmp);
    setFlag(Z, a == tmp);
    setFlag(N, (a - tmp) & 0x80);
    return 0;
}

uint8_t CPU::ISC() {  
    fetch();
    const uint8_t m = static_cast<uint8_t>(fetched_ + 1);
    write(addr_abs_, m);

    const uint16_t value = static_cast<uint16_t>(m) ^ 0x00FF;
    const uint16_t tmp   = static_cast<uint16_t>(a) + value + (getFlag(C) ? 1 : 0);
    setFlag(C, (tmp & 0xFF00) != 0);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(V, (tmp ^ static_cast<uint16_t>(a)) & (tmp ^ value) & 0x0080);
    setFlag(N, tmp & 0x80);
    a = static_cast<uint8_t>(tmp);
    return 0;
}

uint8_t CPU::SLO() {  
    fetch();
    const uint16_t shifted = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, (shifted & 0xFF00) != 0);
    const uint8_t m = static_cast<uint8_t>(shifted);
    write(addr_abs_, m);
    a |= m;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 0;
}

uint8_t CPU::RLA() { 
    fetch();
    const uint16_t rotated = static_cast<uint16_t>(fetched_ << 1) | (getFlag(C) ? 1 : 0);
    setFlag(C, (rotated & 0xFF00) != 0);
    const uint8_t m = static_cast<uint8_t>(rotated);
    write(addr_abs_, m);
    a &= m;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 0;
}

uint8_t CPU::SRE() {  // LSR then EOR
    fetch();
    setFlag(C, fetched_ & 0x01);
    const uint8_t m = static_cast<uint8_t>(fetched_ >> 1);
    write(addr_abs_, m);
    a ^= m;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 0;
}

uint8_t CPU::RRA() { 
    fetch();
    const uint8_t m = static_cast<uint8_t>((getFlag(C) ? 0x80 : 0x00) | (fetched_ >> 1));
    const bool new_carry = (fetched_ & 0x01) != 0;
    write(addr_abs_, m);

    const uint16_t tmp = static_cast<uint16_t>(a) + m + (new_carry ? 1 : 0);
    setFlag(C, tmp > 0xFF);
    setFlag(Z, (tmp & 0xFF) == 0);
    setFlag(V, (~(static_cast<uint16_t>(a) ^ m) &
                 (static_cast<uint16_t>(a) ^ tmp)) & 0x0080);
    setFlag(N, tmp & 0x80);
    a = static_cast<uint8_t>(tmp);
    return 0;
}

uint8_t CPU::ANC() {  
    fetch();
    a &= fetched_;
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    setFlag(C, a & 0x80);
    return 0;
}

uint8_t CPU::ALR() { 
    fetch();
    a &= fetched_;
    setFlag(C, a & 0x01);
    a = static_cast<uint8_t>(a >> 1);
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    return 0;
}

uint8_t CPU::ARR() {  
    fetch();
    a &= fetched_;
    a = static_cast<uint8_t>((getFlag(C) ? 0x80 : 0x00) | (a >> 1));
    setFlag(Z, a == 0);
    setFlag(N, a & 0x80);
    setFlag(C, a & 0x40);
    setFlag(V, ((a >> 6) ^ (a >> 5)) & 0x01);
    return 0;
}

uint8_t CPU::AXS() {  // X = (A AND X) - immediate
    fetch();
    const uint8_t  lhs = static_cast<uint8_t>(a & x);
    const uint16_t tmp = static_cast<uint16_t>(lhs) - fetched_;
    setFlag(C, lhs >= fetched_);
    x = static_cast<uint8_t>(tmp);
    setFlag(Z, x == 0);
    setFlag(N, x & 0x80);
    return 0;
}

uint8_t CPU::XXX() { return 0; }


namespace {
}

std::string CPU::trace() {
    const Instruction& ins = kTable[peek(pc)];

    int len = 3;
    if (ins.mode == &CPU::IMP || ins.mode == &CPU::ACC) {
        len = 1;
    } else if (ins.mode == &CPU::IMM || ins.mode == &CPU::ZP0 ||
               ins.mode == &CPU::ZPX || ins.mode == &CPU::ZPY ||
               ins.mode == &CPU::REL || ins.mode == &CPU::IZX ||
               ins.mode == &CPU::IZY) {
        len = 2;
    }

    char bytes[16] = {0};
    switch (len) {
    case 1: std::snprintf(bytes, sizeof(bytes), "%02X      ", peek(pc)); break;
    case 2: std::snprintf(bytes, sizeof(bytes), "%02X %02X   ",
                          peek(pc), peek(pc + 1)); break;
    default: std::snprintf(bytes, sizeof(bytes), "%02X %02X %02X",
                           peek(pc), peek(pc + 1), peek(pc + 2)); break;
    }

    char line[128];
    std::snprintf(line, sizeof(line),
                  "%04X  %s  %s   A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
                  pc, bytes, ins.name, a, x, y, status, sp,
                  static_cast<unsigned long long>(total_cycles_));
    return std::string(line);
}

}  // namespace nes