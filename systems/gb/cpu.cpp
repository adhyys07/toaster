#include "cpu.h"

namespace emu::gb {

namespace {

constexpr uint8_t FLAG_Z = 0x80;
constexpr uint8_t FLAG_N = 0x40;
constexpr uint8_t FLAG_H = 0x20;
constexpr uint8_t FLAG_C = 0x10;

}

CPU::CPU(Memory& memory, Timer& timer)
    : memory_(memory), timer_(timer) {
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
    halted_ = false;
}

void CPU::setInput(uint8_t buttons) {
    memory_.setButtons(buttons);
}

uint8_t CPU::fetch8() {
    const uint8_t value = memory_.read(pc_);
    pc_ = static_cast<uint16_t>(pc_ + 1);
    return value;
}

uint16_t CPU::fetch16() {
    const uint16_t low = fetch8();
    const uint16_t high = fetch8();
    return static_cast<uint16_t>(low | (high << 8));
}

uint16_t CPU::hl() const {
    return static_cast<uint16_t>((h_ << 8) | l_);
}

void CPU::setHl(uint16_t value) {
    h_ = static_cast<uint8_t>(value >> 8);
    l_ = static_cast<uint8_t>(value & 0xFF);
}

uint8_t CPU::readHl() const {
    return const_cast<Memory&>(memory_).read(hl());
}

void CPU::writeHl(uint8_t value) {
    memory_.write(hl(), value);
}

void CPU::push16(uint16_t value) {
    sp_ = static_cast<uint16_t>(sp_ - 1);
    memory_.write(sp_, static_cast<uint8_t>(value >> 8));

    sp_ = static_cast<uint16_t>(sp_ - 1);
    memory_.write(sp_, static_cast<uint8_t>(value & 0xFF));
}

uint16_t CPU::pop16() {
    const uint16_t low = memory_.read(sp_);
    sp_ = static_cast<uint16_t>(sp_ + 1);

    const uint16_t high = memory_.read(sp_);
    sp_ = static_cast<uint16_t>(sp_ + 1);

    return static_cast<uint16_t>(low | (high << 8));
}

void CPU::setFlag(uint8_t mask, bool value) {
    if (value) {
        f_ |= mask;
    } else {
        f_ &= static_cast<uint8_t>(~mask);
    }

    f_ &= 0xF0;
}

bool CPU::flag(uint8_t mask) const {
    return (f_ & mask) != 0;
}

void CPU::inc8(uint8_t& value) {
    const uint8_t old = value;
    value = static_cast<uint8_t>(value + 1);

    setFlag(FLAG_Z, value == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, (old & 0x0F) == 0x0F);
}

void CPU::dec8(uint8_t& value) {
    const uint8_t old = value;
    value = static_cast<uint8_t>(value - 1);

    setFlag(FLAG_Z, value == 0);
    setFlag(FLAG_N, true);
    setFlag(FLAG_H, (old & 0x0F) == 0);
}

unsigned CPU::step() {
    if (halted_) {
        timer_.tick(4);
        return 4;
    }

    const uint8_t opcode = fetch8();
    executeOpcode(opcode);

    timer_.tick(4);
    return 4;
}

void CPU::executeOpcode(uint8_t opcode) {
    switch (opcode) {
    case 0x00:
        return;

    case 0x01:
        c_ = fetch8();
        b_ = fetch8();
        return;

    case 0x02:
        memory_.write(
            static_cast<uint16_t>((b_ << 8) | c_), a_);
        return;

    case 0x03: {
        const uint16_t value =
            static_cast<uint16_t>((b_ << 8) | c_) + 1;
        b_ = static_cast<uint8_t>(value >> 8);
        c_ = static_cast<uint8_t>(value);
        return;
    }

    case 0x04:
        inc8(b_);
        return;

    case 0x05:
        dec8(b_);
        return;

    case 0x06:
        b_ = fetch8();
        return;

    case 0x0E:
        c_ = fetch8();
        return;

    case 0x11:
        e_ = fetch8();
        d_ = fetch8();
        return;

    case 0x16:
        d_ = fetch8();
        return;

    case 0x1E:
        e_ = fetch8();
        return;

    case 0x21:
        setHl(fetch16());
        return;

    case 0x22: {
        const uint16_t address = hl();
        memory_.write(address, a_);
        setHl(static_cast<uint16_t>(address + 1));
        return;
    }

    case 0x2A: {
        const uint16_t address = hl();
        a_ = memory_.read(address);
        setHl(static_cast<uint16_t>(address + 1));
        return;
    }

    case 0x31:
        sp_ = fetch16();
        return;

    case 0x32: {
        const uint16_t address = hl();
        memory_.write(address, a_);
        setHl(static_cast<uint16_t>(address - 1));
        return;
    }

    case 0x3E:
        a_ = fetch8();
        return;

    case 0x3C:
        inc8(a_);
        return;

    case 0x3D:
        dec8(a_);
        return;

    case 0x76:
        halted_ = true;
        return;

    case 0x77:
        writeHl(a_);
        return;

    case 0x7E:
        a_ = readHl();
        return;

    case 0x78:
        a_ = b_;
        return;

    case 0x79:
        a_ = c_;
        return;

    case 0x7A:
        a_ = d_;
        return;

    case 0x7B:
        a_ = e_;
        return;

    case 0x7C:
        a_ = h_;
        return;

    case 0x7D:
        a_ = l_;
        return;

    case 0x80: {
        const uint16_t result = a_ + b_;
        setFlag(FLAG_Z, static_cast<uint8_t>(result) == 0);
        setFlag(FLAG_N, false);
        setFlag(FLAG_H, ((a_ & 0x0F) + (b_ & 0x0F)) > 0x0F);
        setFlag(FLAG_C, result > 0xFF);
        a_ = static_cast<uint8_t>(result);
        return;
    }

    case 0xAF:
        a_ ^= a_;
        f_ = FLAG_Z;
        return;

    case 0xC3:
        pc_ = fetch16();
        return;

    case 0xC9:
        pc_ = pop16();
        return;

    case 0xCD: {
        const uint16_t target = fetch16();
        push16(pc_);
        pc_ = target;
        return;
    }

    case 0xC5:
        push16(static_cast<uint16_t>((b_ << 8) | c_));
        return;

    case 0xD5:
        push16(static_cast<uint16_t>((d_ << 8) | e_));
        return;

    case 0xE5:
        push16(hl());
        return;

    case 0xF5:
        push16(static_cast<uint16_t>((a_ << 8) | f_));
        return;

    case 0xC1: {
        const uint16_t value = pop16();
        b_ = static_cast<uint8_t>(value >> 8);
        c_ = static_cast<uint8_t>(value);
        return;
    }

    case 0xD1: {
        const uint16_t value = pop16();
        d_ = static_cast<uint8_t>(value >> 8);
        e_ = static_cast<uint8_t>(value);
        return;
    }

    case 0xE1:
        setHl(pop16());
        return;

    case 0xF1: {
        const uint16_t value = pop16();
        a_ = static_cast<uint8_t>(value >> 8);
        f_ = static_cast<uint8_t>(value & 0xF0);
        return;
    }

    case 0xCB:
        executeCB(fetch8());
        return;

    case 0xF3:
        ime_ = false;
        return;

    case 0xFB:
        ime_ = true;
        return;

    default:
        return;
    }
}

void CPU::executeCB(uint8_t opcode) {
    switch (opcode) {
    case 0x00: {
        const bool carry = (b_ & 0x80) != 0;
        b_ = static_cast<uint8_t>((b_ << 1) | (carry ? 1 : 0));
        f_ = 0;
        setFlag(FLAG_Z, b_ == 0);
        setFlag(FLAG_C, carry);
        return;
    }

    case 0x11: {
        const bool carry_in = flag(FLAG_C);
        const bool carry_out = (c_ & 0x80) != 0;
        c_ = static_cast<uint8_t>((c_ << 1) | (carry_in ? 1 : 0));
        f_ = 0;
        setFlag(FLAG_Z, c_ == 0);
        setFlag(FLAG_C, carry_out);
        return;
    }

    case 0x7C:
        setFlag(FLAG_Z, (h_ & 0x80) == 0);
        setFlag(FLAG_N, false);
        setFlag(FLAG_H, true);
        return;

    default:
        return;
    }
}

} // namespace emu::gb