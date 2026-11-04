#include "cpu.h"

#include "apu.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

namespace emu::gb {
namespace {

constexpr uint8_t kFlagZ = 0x80;
constexpr uint8_t kFlagN = 0x40;
constexpr uint8_t kFlagH = 0x20;
constexpr uint8_t kFlagC = 0x10;

} // namespace

CPU::CPU(Memory& memory, Timer& timer, PPU& ppu, APU& apu)
    : memory_(memory), timer_(timer), ppu_(ppu), apu_(apu) {
    reset();
}

void CPU::reset() {
    a_ = 0x01;
    f_ = 0xB0;
    b_ = 0x00;
    c_ = 0x13;
    d_ = 0x00;
    e_ = 0xD8;
    h_ = 0x01;
    l_ = 0x4D;
    pc_ = 0x0100;
    sp_ = 0xFFFE;

    ime_ = false;
    ime_pending_ = false;
    halted_ = false;
    halt_bug_ = false;
    stopped_ = false;
    cycles_ = 0;
}

void CPU::setInput(uint8_t buttons) {
    memory_.setButtons(buttons);
}

void CPU::tick() {
    cycles_ += 4;
    timer_.tick(4);
    ppu_.tick(4);
    apu_.tick(4);
}

uint8_t CPU::read8(uint16_t address) {
    tick();
    return memory_.read(address);
}

void CPU::write8(uint16_t address, uint8_t value) {
    tick();
    memory_.write(address, value);
}

uint8_t CPU::fetch8() {
    const uint8_t value = read8(pc_);
    if (halt_bug_) {
        halt_bug_ = false;
    } else {
        ++pc_;
    }
    return value;
}

uint16_t CPU::fetch16() {
    const uint8_t low = fetch8();
    const uint8_t high = fetch8();
    return static_cast<uint16_t>((high << 8) | low);
}

void CPU::push16(uint16_t value) {
    write8(--sp_, static_cast<uint8_t>(value >> 8));
    write8(--sp_, static_cast<uint8_t>(value & 0xFF));
}

uint16_t CPU::pop16() {
    const uint8_t low = read8(sp_++);
    const uint8_t high = read8(sp_++);
    return static_cast<uint16_t>((high << 8) | low);
}

void CPU::setFlag(uint8_t mask, bool value) {
    if (value) {
        f_ |= mask;
    } else {
        f_ = static_cast<uint8_t>(f_ & ~mask);
    }
    f_ = static_cast<uint8_t>(f_ & 0xF0);
}

bool CPU::flag(uint8_t mask) const {
    return (f_ & mask) != 0;
}

uint16_t CPU::hl() const {
    return static_cast<uint16_t>((h_ << 8) | l_);
}

void CPU::setHl(uint16_t value) {
    h_ = static_cast<uint8_t>(value >> 8);
    l_ = static_cast<uint8_t>(value & 0xFF);
}

uint8_t CPU::readReg(int index) {
    switch (index) {
    case 0: return b_;
    case 1: return c_;
    case 2: return d_;
    case 3: return e_;
    case 4: return h_;
    case 5: return l_;
    case 6: return read8(hl());        
    default: return a_;
    }
}

void CPU::writeReg(int index, uint8_t value) {
    switch (index) {
    case 0: b_ = value; break;
    case 1: c_ = value; break;
    case 2: d_ = value; break;
    case 3: e_ = value; break;
    case 4: h_ = value; break;
    case 5: l_ = value; break;
    case 6: write8(hl(), value); break;
    default: a_ = value; break;
    }
}

uint16_t CPU::readPair(int index) const {
    switch (index) {
    case 0: return static_cast<uint16_t>((b_ << 8) | c_);
    case 1: return static_cast<uint16_t>((d_ << 8) | e_);
    case 2: return hl();
    default: return sp_;
    }
}

void CPU::writePair(int index, uint16_t value) {
    switch (index) {
    case 0:
        b_ = static_cast<uint8_t>(value >> 8);
        c_ = static_cast<uint8_t>(value & 0xFF);
        break;
    case 1:
        d_ = static_cast<uint8_t>(value >> 8);
        e_ = static_cast<uint8_t>(value & 0xFF);
        break;
    case 2:
        setHl(value);
        break;
    default:
        sp_ = value;
        break;
    }
}

bool CPU::condition(int index) const {
    switch (index) {
    case 0: return !flag(kFlagZ);
    case 1: return flag(kFlagZ);
    case 2: return !flag(kFlagC);
    default: return flag(kFlagC);
    }
}

void CPU::inc8(uint8_t& value) {
    const uint8_t old = value;
    value = static_cast<uint8_t>(value + 1);
    setFlag(kFlagZ, value == 0);
    setFlag(kFlagN, false);
    setFlag(kFlagH, (old & 0x0F) == 0x0F);
}

void CPU::dec8(uint8_t& value) {
    const uint8_t old = value;
    value = static_cast<uint8_t>(value - 1);
    setFlag(kFlagZ, value == 0);
    setFlag(kFlagN, true);
    setFlag(kFlagH, (old & 0x0F) == 0x00);
}

void CPU::alu(int operation, uint8_t operand) {
    const uint8_t carry = flag(kFlagC) ? 1 : 0;

    switch (operation) {
    case 0: {  
        const unsigned result = a_ + operand;
        setFlag(kFlagH, ((a_ & 0x0F) + (operand & 0x0F)) > 0x0F);
        setFlag(kFlagC, result > 0xFF);
        a_ = static_cast<uint8_t>(result);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, false);
        break;
    }
    case 1: {  
        const unsigned result = a_ + operand + carry;
        setFlag(kFlagH, ((a_ & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
        setFlag(kFlagC, result > 0xFF);
        a_ = static_cast<uint8_t>(result);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, false);
        break;
    }
    case 2: { 
        setFlag(kFlagH, (a_ & 0x0F) < (operand & 0x0F));
        setFlag(kFlagC, a_ < operand);
        a_ = static_cast<uint8_t>(a_ - operand);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, true);
        break;
    }
    case 3: {  
        const int result = static_cast<int>(a_) - operand - carry;
        setFlag(kFlagH, (a_ & 0x0F) < ((operand & 0x0F) + carry));
        setFlag(kFlagC, result < 0);
        a_ = static_cast<uint8_t>(result);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, true);
        break;
    }
    case 4:  
        a_ = static_cast<uint8_t>(a_ & operand);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, false);
        setFlag(kFlagH, true);
        setFlag(kFlagC, false);
        break;
    case 5:  // XOR
        a_ = static_cast<uint8_t>(a_ ^ operand);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, false);
        setFlag(kFlagH, false);
        setFlag(kFlagC, false);
        break;
    case 6: 
        a_ = static_cast<uint8_t>(a_ | operand);
        setFlag(kFlagZ, a_ == 0);
        setFlag(kFlagN, false);
        setFlag(kFlagH, false);
        setFlag(kFlagC, false);
        break;
    default: 
        setFlag(kFlagZ, a_ == operand);
        setFlag(kFlagN, true);
        setFlag(kFlagH, (a_ & 0x0F) < (operand & 0x0F));
        setFlag(kFlagC, a_ < operand);
        break;
    }
}

uint8_t CPU::rotate(int operation, uint8_t value) {
    const bool carry_in = flag(kFlagC);
    uint8_t result = value;

    switch (operation) {
    case 0:  // RLC
        setFlag(kFlagC, (value & 0x80) != 0);
        result = static_cast<uint8_t>((value << 1) | (value >> 7));
        break;
    case 1:  // RRC
        setFlag(kFlagC, (value & 0x01) != 0);
        result = static_cast<uint8_t>((value >> 1) | (value << 7));
        break;
    case 2:  // RL
        setFlag(kFlagC, (value & 0x80) != 0);
        result = static_cast<uint8_t>((value << 1) | (carry_in ? 1 : 0));
        break;
    case 3:  // RR
        setFlag(kFlagC, (value & 0x01) != 0);
        result = static_cast<uint8_t>((value >> 1) | (carry_in ? 0x80 : 0));
        break;
    case 4:  // SLA
        setFlag(kFlagC, (value & 0x80) != 0);
        result = static_cast<uint8_t>(value << 1);
        break;
    case 5:
        setFlag(kFlagC, (value & 0x01) != 0);
        result = static_cast<uint8_t>((value >> 1) | (value & 0x80));
        break;
    case 6: 
        result = static_cast<uint8_t>((value << 4) | (value >> 4));
        setFlag(kFlagC, false);
        break;
    default:  // SRL
        setFlag(kFlagC, (value & 0x01) != 0);
        result = static_cast<uint8_t>(value >> 1);
        break;
    }

    setFlag(kFlagZ, result == 0);
    setFlag(kFlagN, false);
    setFlag(kFlagH, false);
    return result;
}

void CPU::daa() {
    int adjust = 0;
    bool carry = flag(kFlagC);

    if (flag(kFlagN)) {
        if (flag(kFlagH)) adjust -= 0x06;
        if (carry) adjust -= 0x60;
    } else {
        if (flag(kFlagH) || (a_ & 0x0F) > 0x09) adjust += 0x06;
        if (carry || a_ > 0x99) {
            adjust += 0x60;
            carry = true;
        }
    }

    a_ = static_cast<uint8_t>(a_ + adjust);
    setFlag(kFlagZ, a_ == 0);
    setFlag(kFlagH, false);
    setFlag(kFlagC, carry);
}

void CPU::addToHl(uint16_t operand) {
    const unsigned result = hl() + operand;
    setFlag(kFlagH, ((hl() & 0x0FFF) + (operand & 0x0FFF)) > 0x0FFF);
    setFlag(kFlagC, result > 0xFFFF);
    setFlag(kFlagN, false);
    setHl(static_cast<uint16_t>(result));
    tick();   // 16-bit adds take an extra machine cycle
}

unsigned CPU::serviceInterrupts() {
    const uint8_t pending = static_cast<uint8_t>(
        memory_.interruptFlags() & memory_.interruptEnable() & 0x1F);
    if (pending == 0) return 0;

    halted_ = false;
    stopped_ = false;
    if (!ime_) return 0;

    for (int bit = 0; bit < 5; ++bit) {
        if ((pending & (1 << bit)) == 0) continue;

        ime_ = false;
        ime_pending_ = false;
        memory_.clearInterrupt(static_cast<uint8_t>(1 << bit));

        cycles_ = 0;
        tick();
        tick();
        push16(pc_);
        pc_ = static_cast<uint16_t>(0x40 + bit * 8);
        tick();
        return cycles_;   // 20 T-cycles in total
    }

    return 0;
}

unsigned CPU::step() {
    cycles_ = 0;

    const bool enable_after = ime_pending_;

    if (const unsigned serviced = serviceInterrupts()) {
        if (enable_after) {
            ime_ = true;
            ime_pending_ = false;
        }
        return serviced;
    }

    if (enable_after) {
        ime_ = true;
        ime_pending_ = false;
    }

    if (halted_ || stopped_) {
        tick();
        return cycles_;
    }

    const uint8_t opcode = fetch8();
    executeOpcode(opcode);
    return cycles_;
}

void CPU::executeOpcode(uint8_t opcode) {
    // 0x40..0x7F is LD r,r' with 0x76 carved out for HALT.
    if (opcode >= 0x40 && opcode <= 0x7F) {
        if (opcode == 0x76) {
            const uint8_t pending = static_cast<uint8_t>(
                memory_.interruptFlags() & memory_.interruptEnable() & 0x1F);
            if (!ime_ && pending != 0) {
                halt_bug_ = true;      // does not halt; re-reads the next byte
            } else {
                halted_ = true;
            }
            return;
        }
        writeReg((opcode >> 3) & 7, readReg(opcode & 7));
        return;
    }

    // 0x80..0xBF is an ALU operation on the accumulator.
    if (opcode >= 0x80 && opcode <= 0xBF) {
        alu((opcode >> 3) & 7, readReg(opcode & 7));
        return;
    }

    switch (opcode) {
    case 0x00:  // NOP
        return;

    case 0x10:  // STOP
        fetch8();
        stopped_ = true;
        return;

    case 0xCB:
        executeCB(fetch8());
        return;

    // --- 16-bit loads and arithmetic ---
    case 0x01: case 0x11: case 0x21: case 0x31:
        writePair((opcode >> 4) & 3, fetch16());
        return;

    case 0x09: case 0x19: case 0x29: case 0x39:
        addToHl(readPair((opcode >> 4) & 3));
        return;

    case 0x03: case 0x13: case 0x23: case 0x33: {
        const int pair = (opcode >> 4) & 3;
        writePair(pair, static_cast<uint16_t>(readPair(pair) + 1));
        tick();
        return;
    }

    case 0x0B: case 0x1B: case 0x2B: case 0x3B: {
        const int pair = (opcode >> 4) & 3;
        writePair(pair, static_cast<uint16_t>(readPair(pair) - 1));
        tick();
        return;
    }

    // --- 8-bit increment, decrement and immediate load ---
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C: {
        const int target = (opcode >> 3) & 7;
        uint8_t value = readReg(target);
        inc8(value);
        writeReg(target, value);
        return;
    }

    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D: {
        const int target = (opcode >> 3) & 7;
        uint8_t value = readReg(target);
        dec8(value);
        writeReg(target, value);
        return;
    }

    case 0x06: case 0x0E: case 0x16: case 0x1E:
    case 0x26: case 0x2E: case 0x36: case 0x3E:
        writeReg((opcode >> 3) & 7, fetch8());
        return;

    // --- indirect loads ---
    case 0x02: write8(readPair(0), a_); return;
    case 0x12: write8(readPair(1), a_); return;
    case 0x22: write8(hl(), a_); setHl(static_cast<uint16_t>(hl() + 1)); return;
    case 0x32: write8(hl(), a_); setHl(static_cast<uint16_t>(hl() - 1)); return;
    case 0x0A: a_ = read8(readPair(0)); return;
    case 0x1A: a_ = read8(readPair(1)); return;
    case 0x2A: a_ = read8(hl()); setHl(static_cast<uint16_t>(hl() + 1)); return;
    case 0x3A: a_ = read8(hl()); setHl(static_cast<uint16_t>(hl() - 1)); return;

    // --- accumulator rotates: unlike the CB forms these always clear Z ---
    case 0x07: a_ = rotate(0, a_); setFlag(kFlagZ, false); return;
    case 0x0F: a_ = rotate(1, a_); setFlag(kFlagZ, false); return;
    case 0x17: a_ = rotate(2, a_); setFlag(kFlagZ, false); return;
    case 0x1F: a_ = rotate(3, a_); setFlag(kFlagZ, false); return;

    case 0x27: daa(); return;
    case 0x2F:
        a_ = static_cast<uint8_t>(~a_);
        setFlag(kFlagN, true);
        setFlag(kFlagH, true);
        return;
    case 0x37:
        setFlag(kFlagC, true);
        setFlag(kFlagN, false);
        setFlag(kFlagH, false);
        return;
    case 0x3F:
        setFlag(kFlagC, !flag(kFlagC));
        setFlag(kFlagN, false);
        setFlag(kFlagH, false);
        return;

    // --- relative jumps ---
    case 0x18: {
        const auto offset = static_cast<int8_t>(fetch8());
        pc_ = static_cast<uint16_t>(pc_ + offset);
        tick();
        return;
    }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        const auto offset = static_cast<int8_t>(fetch8());
        if (condition((opcode >> 3) & 3)) {
            pc_ = static_cast<uint16_t>(pc_ + offset);
            tick();
        }
        return;
    }

    // --- absolute jumps, calls and returns ---
    case 0xC3:
        pc_ = fetch16();
        tick();
        return;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
        const uint16_t target = fetch16();
        if (condition((opcode >> 3) & 3)) {
            pc_ = target;
            tick();
        }
        return;
    }
    case 0xE9:
        pc_ = hl();
        return;

    case 0xCD: {
        const uint16_t target = fetch16();
        tick();
        push16(pc_);
        pc_ = target;
        return;
    }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
        const uint16_t target = fetch16();
        if (condition((opcode >> 3) & 3)) {
            tick();
            push16(pc_);
            pc_ = target;
        }
        return;
    }

    case 0xC9:
        pc_ = pop16();
        tick();
        return;
    case 0xD9:
        pc_ = pop16();
        ime_ = true;
        ime_pending_ = false;
        tick();
        return;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        tick();
        if (condition((opcode >> 3) & 3)) {
            pc_ = pop16();
            tick();
        }
        return;

    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        tick();
        push16(pc_);
        pc_ = static_cast<uint16_t>(opcode & 0x38);
        return;

    // --- stack ---
    case 0xC1: case 0xD1: case 0xE1:
        writePair((opcode >> 4) & 3, pop16());
        return;
    case 0xF1: {
        const uint16_t value = pop16();
        a_ = static_cast<uint8_t>(value >> 8);
        f_ = static_cast<uint8_t>(value & 0xF0);
        return;
    }
    case 0xC5: case 0xD5: case 0xE5:
        tick();
        push16(readPair((opcode >> 4) & 3));
        return;
    case 0xF5:
        tick();
        push16(static_cast<uint16_t>((a_ << 8) | f_));
        return;

    // --- ALU with an immediate operand ---
    case 0xC6: case 0xCE: case 0xD6: case 0xDE:
    case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu((opcode >> 3) & 7, fetch8());
        return;

    // --- high RAM and absolute addressing ---
    case 0xE0:
        write8(static_cast<uint16_t>(0xFF00 + fetch8()), a_);
        return;
    case 0xF0:
        a_ = read8(static_cast<uint16_t>(0xFF00 + fetch8()));
        return;
    case 0xE2:
        write8(static_cast<uint16_t>(0xFF00 + c_), a_);
        return;
    case 0xF2:
        a_ = read8(static_cast<uint16_t>(0xFF00 + c_));
        return;
    case 0xEA:
        write8(fetch16(), a_);
        return;
    case 0xFA:
        a_ = read8(fetch16());
        return;
    case 0x08: {
        const uint16_t target = fetch16();
        write8(target, static_cast<uint8_t>(sp_ & 0xFF));
        write8(static_cast<uint16_t>(target + 1),
               static_cast<uint8_t>(sp_ >> 8));
        return;
    }

    // --- stack pointer arithmetic: H and C come from the low byte, and both
    //     of these clear Z and N regardless of the result ---
    case 0xE8: {
        const auto offset = static_cast<int8_t>(fetch8());
        setFlag(kFlagH, ((sp_ & 0x0F) + (offset & 0x0F)) > 0x0F);
        setFlag(kFlagC, ((sp_ & 0xFF) + (offset & 0xFF)) > 0xFF);
        setFlag(kFlagZ, false);
        setFlag(kFlagN, false);
        sp_ = static_cast<uint16_t>(sp_ + offset);
        tick();
        tick();
        return;
    }
    case 0xF8: {
        const auto offset = static_cast<int8_t>(fetch8());
        setFlag(kFlagH, ((sp_ & 0x0F) + (offset & 0x0F)) > 0x0F);
        setFlag(kFlagC, ((sp_ & 0xFF) + (offset & 0xFF)) > 0xFF);
        setFlag(kFlagZ, false);
        setFlag(kFlagN, false);
        setHl(static_cast<uint16_t>(sp_ + offset));
        tick();
        return;
    }
    case 0xF9:
        sp_ = hl();
        tick();
        return;

    case 0xF3:
        ime_ = false;
        ime_pending_ = false;
        return;
    case 0xFB:
        ime_pending_ = true;
        return;

    default:
        // D3, DB, DD, E3, E4, EB, EC, ED, F4, FC and FD do not decode and lock
        // up real hardware. Behaving as a NOP keeps a buggy ROM from hanging
        // the emulator instead.
        return;
    }
}

void CPU::executeCB(uint8_t opcode) {
    const int target = opcode & 7;
    const int bit = (opcode >> 3) & 7;

    if (opcode < 0x40) {
        writeReg(target, rotate(bit, readReg(target)));
        return;
    }

    if (opcode < 0x80) {  // BIT
        setFlag(kFlagZ, (readReg(target) & (1 << bit)) == 0);
        setFlag(kFlagN, false);
        setFlag(kFlagH, true);
        return;
    }

    if (opcode < 0xC0) {  // RES
        writeReg(target,
                 static_cast<uint8_t>(readReg(target) & ~(1 << bit)));
        return;
    }

    // SET
    writeReg(target, static_cast<uint8_t>(readReg(target) | (1 << bit)));
}

} // namespace emu::gb
