// Unit tests for the Game Boy core. These drive the components directly
// rather than through a game, so a failure points at one instruction or one
// register rather than "the picture looks wrong".
//
// The CPU boots at 0x0100 in cartridge space, and there is no way to set PC
// from outside it, so every test runs behind a one-instruction trampoline ROM
// holding `JP C000`. Each test program is written into work RAM, which is both
// writable and executable, and reached by stepping that JP.
#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void group(const char* name) {
    std::printf("\n%s\n", name);
}

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

template <typename T, typename U>
void checkEqual(T actual, U expected, const std::string& what) {
    const auto left = static_cast<unsigned long long>(actual);
    const auto right = static_cast<unsigned long long>(expected);
    if (left == right) {
        check(true, what);
        return;
    }
    char detail[256];
    std::snprintf(detail, sizeof(detail), "%s (got 0x%llX, wanted 0x%llX)",
                  what.c_str(), left, right);
    check(false, detail);
}

constexpr uint8_t kFlagZ = 0x80;
constexpr uint8_t kFlagN = 0x40;
constexpr uint8_t kFlagH = 0x20;
constexpr uint8_t kFlagC = 0x10;

// Writes the trampoline ROM once and returns its path.
const std::string& trampolineRom() {
    static std::string path;
    if (!path.empty()) return path;

    path = "gb_test_trampoline.gb";
    std::vector<uint8_t> rom(32 * 1024, 0);

    rom[0x0100] = 0xC3;   // JP 0xC000
    rom[0x0101] = 0x00;
    rom[0x0102] = 0xC0;

    const char* title = "TESTGB";
    std::memcpy(&rom[0x0134], title, std::strlen(title));
    rom[0x0147] = 0x03;   // MBC1 + RAM + battery
    rom[0x0148] = 0x00;   // 32 KB
    rom[0x0149] = 0x02;   // 8 KB of cartridge RAM

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(rom.data()),
               static_cast<std::streamsize>(rom.size()));
    return path;
}

class Machine {
public:
    static constexpr uint16_t kCode = 0xC000;

    Machine() {
        memory_.connect(&cartridge_);
        memory_.connect(&ppu_, &apu_, &timer_);

        std::string error;
        if (!cartridge_.load(trampolineRom(), error)) {
            std::printf("  cannot load the trampoline ROM: %s\n",
                        error.c_str());
            std::exit(EXIT_FAILURE);
        }
        cpu_.reset();
    }

    // Loads a program into work RAM and steps the trampoline so PC reaches it.
    void start(std::initializer_list<uint8_t> program) {
        uint16_t address = kCode;
        for (uint8_t byte : program) memory_.write(address++, byte);
        cpu_.reset();
        cpu_.step();   // the JP into work RAM
    }

    unsigned step() { return cpu_.step(); }

    void steps(int count) {
        for (int i = 0; i < count; ++i) cpu_.step();
    }

    emu::gb::Memory& memory() { return memory_; }
    emu::gb::Timer& timer() { return timer_; }
    emu::gb::PPU& ppu() { return ppu_; }
    emu::gb::APU& apu() { return apu_; }
    emu::gb::CPU& cpu() { return cpu_; }
    emu::gb::Cartridge& cartridge() { return cartridge_; }

private:
    emu::gb::Cartridge cartridge_;
    emu::gb::Memory memory_;
    emu::gb::Timer timer_{memory_};
    emu::gb::PPU ppu_{memory_};
    emu::gb::APU apu_;
    emu::gb::CPU cpu_{memory_, timer_, ppu_, apu_};
};

// ---------------------------------------------------------------------------

void testLoadsAndAlu() {
    group("8-bit loads and ALU");

    {   // LD A,n / LD B,n / ADD A,B with a half-carry
        Machine m;
        m.start({0x3E, 0x0F, 0x06, 0x01, 0x80});
        m.steps(2);
        m.step();
        checkEqual(m.cpu().a(), 0x10, "ADD A,B result");
        check((m.cpu().f() & kFlagH) != 0, "ADD sets H on a nibble carry");
        check((m.cpu().f() & kFlagC) == 0, "ADD leaves C clear without a carry");
        check((m.cpu().f() & kFlagN) == 0, "ADD clears N");
    }

    {   // ADD that overflows the byte
        Machine m;
        m.start({0x3E, 0xFF, 0x06, 0x01, 0x80});
        m.steps(3);
        checkEqual(m.cpu().a(), 0x00, "ADD wraps to zero");
        check((m.cpu().f() & kFlagZ) != 0, "ADD sets Z on a zero result");
        check((m.cpu().f() & kFlagC) != 0, "ADD sets C on a byte carry");
    }

    {   // ADC picking up the carry
        Machine m;
        m.start({0x3E, 0xFF, 0x06, 0x01, 0x80,        // A = 0, C = 1
                 0x3E, 0x05, 0x06, 0x05, 0x88});      // ADC A,B
        m.steps(5);
        m.steps(3);
        checkEqual(m.cpu().a(), 0x0B, "ADC adds the carry in");
    }

    {   // SUB and CP
        Machine m;
        m.start({0x3E, 0x10, 0x06, 0x01, 0x90});
        m.steps(3);
        checkEqual(m.cpu().a(), 0x0F, "SUB result");
        check((m.cpu().f() & kFlagN) != 0, "SUB sets N");
        check((m.cpu().f() & kFlagH) != 0, "SUB sets H on a nibble borrow");
    }

    {   // CP must not modify A
        Machine m;
        m.start({0x3E, 0x42, 0xFE, 0x42});
        m.steps(2);
        checkEqual(m.cpu().a(), 0x42, "CP leaves A alone");
        check((m.cpu().f() & kFlagZ) != 0, "CP sets Z when equal");
    }

    {   // AND / OR / XOR flag behaviour
        Machine m;
        m.start({0x3E, 0xF0, 0xE6, 0x0F,              // AND 0x0F -> 0
                 0x3E, 0xF0, 0xF6, 0x0F,              // OR  0x0F -> FF
                 0xEE, 0xFF});                        // XOR FF   -> 0
        m.steps(2);
        checkEqual(m.cpu().a(), 0x00, "AND result");
        check((m.cpu().f() & kFlagH) != 0, "AND sets H");
        check((m.cpu().f() & kFlagZ) != 0, "AND sets Z");
        m.steps(2);
        checkEqual(m.cpu().a(), 0xFF, "OR result");
        check((m.cpu().f() & kFlagH) == 0, "OR clears H");
        m.step();
        checkEqual(m.cpu().a(), 0x00, "XOR result");
    }

    {   // INC and DEC must not touch the carry flag
        Machine m;
        m.start({0x37,                                // SCF -> C = 1
                 0x3E, 0xFF, 0x3C,                    // LD A,FF; INC A
                 0x3D});                              // DEC A
        m.steps(3);
        checkEqual(m.cpu().a(), 0x00, "INC wraps");
        check((m.cpu().f() & kFlagC) != 0, "INC preserves C");
        check((m.cpu().f() & kFlagZ) != 0, "INC sets Z");
        m.step();
        checkEqual(m.cpu().a(), 0xFF, "DEC wraps back");
        check((m.cpu().f() & kFlagN) != 0, "DEC sets N");
    }

    {   // DAA after an addition
        Machine m;
        m.start({0x3E, 0x09, 0x06, 0x01, 0x80, 0x27});
        m.steps(3);
        m.step();
        checkEqual(m.cpu().a(), 0x10, "DAA corrects 0x0A to 0x10");
    }

    {   // All eight register slots, including (HL)
        Machine m;
        m.start({0x21, 0x00, 0xD0,                    // LD HL,0xD000
                 0x3E, 0x5A, 0x77,                    // LD A,5A; LD (HL),A
                 0x06, 0x00, 0x46});                  // LD B,0; LD B,(HL)
        m.steps(5);
        checkEqual(m.memory().read(0xD000), 0x5A, "LD (HL),A stores");
        m.steps(2);
        checkEqual(m.cpu().b(), 0x5A, "LD B,(HL) loads");
    }
}

void testSixteenBit() {
    group("16-bit operations");

    {   // LD rr,nn and ADD HL,rr
        Machine m;
        m.start({0x21, 0xFF, 0x0F,                    // LD HL,0x0FFF
                 0x11, 0x01, 0x00,                    // LD DE,0x0001
                 0x19});                              // ADD HL,DE
        m.steps(2);
        m.step();
        checkEqual((m.cpu().h() << 8) | m.cpu().l(), 0x1000, "ADD HL,DE result");
        check((m.cpu().f() & kFlagH) != 0, "ADD HL sets H from bit 11");
    }

    {   // INC rr must not affect the flags
        Machine m;
        m.start({0x37, 0x01, 0xFF, 0xFF, 0x03});      // SCF; LD BC,FFFF; INC BC
        m.steps(3);
        checkEqual((m.cpu().b() << 8) | m.cpu().c(), 0x0000, "INC BC wraps");
        check((m.cpu().f() & kFlagC) != 0, "INC BC leaves the flags alone");
    }

    {   // PUSH/POP round trip, and the low nibble of F being unwritable
        Machine m;
        m.start({0x01, 0x34, 0x12,                    // LD BC,0x1234
                 0xC5,                                // PUSH BC
                 0xF1});                              // POP AF
        m.steps(3);
        checkEqual(m.cpu().a(), 0x12, "POP AF high byte");
        checkEqual(m.cpu().f(), 0x30, "POP AF masks off the low nibble of F");
    }

    {   // LD (nn),SP writes little endian
        Machine m;
        m.start({0x31, 0xCD, 0xAB,                    // LD SP,0xABCD
                 0x08, 0x00, 0xD0});                  // LD (0xD000),SP
        m.steps(2);
        checkEqual(m.memory().read(0xD000), 0xCD, "LD (nn),SP low byte");
        checkEqual(m.memory().read(0xD001), 0xAB, "LD (nn),SP high byte");
    }

    {   // ADD SP,e with a negative offset, and its flags
        Machine m;
        m.start({0x31, 0x00, 0xD0,                    // LD SP,0xD000
                 0xE8, 0xFF});                        // ADD SP,-1
        m.steps(2);
        checkEqual(m.cpu().sp(), 0xCFFF, "ADD SP,-1 result");
        check((m.cpu().f() & kFlagZ) == 0, "ADD SP,e clears Z");
    }
}

void testRotatesAndBits() {
    group("CB rotates and bit operations");

    {   // RLC through the carry
        Machine m;
        m.start({0x3E, 0x80, 0xCB, 0x07});            // LD A,80; RLC A
        m.steps(2);
        checkEqual(m.cpu().a(), 0x01, "RLC A rotates bit 7 into bit 0");
        check((m.cpu().f() & kFlagC) != 0, "RLC A sets C");
    }

    {   // SRA preserves the sign bit, SRL does not
        Machine m;
        m.start({0x3E, 0x80, 0xCB, 0x2F,              // SRA A
                 0x3E, 0x80, 0xCB, 0x3F});            // SRL A
        m.steps(2);
        checkEqual(m.cpu().a(), 0xC0, "SRA A keeps the sign");
        m.steps(2);
        checkEqual(m.cpu().a(), 0x40, "SRL A shifts in a zero");
    }

    {   // SWAP
        Machine m;
        m.start({0x3E, 0xAB, 0xCB, 0x37});
        m.steps(2);
        checkEqual(m.cpu().a(), 0xBA, "SWAP A exchanges the nibbles");
    }

    {   // BIT, RES and SET, including through (HL)
        Machine m;
        m.start({0x3E, 0x08, 0xCB, 0x5F,              // BIT 3,A
                 0xCB, 0x9F,                          // RES 3,A
                 0xCB, 0xFF});                        // SET 7,A
        m.steps(2);
        check((m.cpu().f() & kFlagZ) == 0, "BIT 3,A clears Z when the bit is set");
        check((m.cpu().f() & kFlagH) != 0, "BIT sets H");
        m.step();
        checkEqual(m.cpu().a(), 0x00, "RES clears the bit");
        m.step();
        checkEqual(m.cpu().a(), 0x80, "SET sets the bit");
    }

    {   // The accumulator rotate always clears Z, unlike its CB twin
        Machine m;
        m.start({0x3E, 0x00, 0x07,                    // LD A,0; RLCA
                 0xCB, 0x07});                        // RLC A
        m.steps(2);
        check((m.cpu().f() & kFlagZ) == 0, "RLCA clears Z even on zero");
        m.step();
        check((m.cpu().f() & kFlagZ) != 0, "RLC A sets Z on zero");
    }
}

void testControlFlow() {
    group("jumps, calls and returns");

    {   // A taken conditional jump costs more than one that falls through
        Machine m;
        m.start({0xAF,                                // XOR A -> Z set
                 0x28, 0x02,                          // JR Z,+2  (taken)
                 0x00, 0x00,
                 0x3E, 0x99});                        // LD A,0x99
        m.step();
        const unsigned taken = m.step();
        checkEqual(taken, 12, "a taken JR costs 12 cycles");
        m.step();
        checkEqual(m.cpu().a(), 0x99, "JR Z skipped the padding");
    }

    {
        Machine m;
        m.start({0x3E, 0x01, 0xB7,                    // LD A,1; OR A -> Z clear
                 0x28, 0x02});                        // JR Z,+2  (not taken)
        m.steps(2);
        const unsigned not_taken = m.step();
        checkEqual(not_taken, 8, "an untaken JR costs 8 cycles");
    }

    {   // CALL pushes the return address and RET comes back to it
        Machine m;
        m.start({0x31, 0x00, 0xDF,                    // LD SP,0xDF00
                 0xCD, 0x0A, 0xC0,                    // CALL 0xC00A
                 0x3E, 0x11,                          // (returns here)
                 0x00, 0x00,
                 0x3E, 0x22, 0xC9});                  // LD A,0x22; RET
        m.steps(2);
        checkEqual(m.cpu().sp(), 0xDEFE, "CALL pushes two bytes");
        m.steps(2);                                   // LD A,0x22 then RET
        checkEqual(m.cpu().a(), 0x22, "the called code ran");
        checkEqual(m.cpu().sp(), 0xDF00, "RET pops the return address");
        m.step();
        checkEqual(m.cpu().a(), 0x11, "execution resumed after the CALL");
    }

    {   // RST jumps to its fixed vector
        Machine m;
        m.start({0x31, 0x00, 0xDF, 0xDF});            // LD SP; RST 0x18
        m.steps(1);
        m.step();
        checkEqual(m.cpu().pc(), 0x0018, "RST 18 jumps to its vector");
    }

    {   // JP (HL)
        Machine m;
        m.start({0x21, 0x0A, 0xC0, 0xE9,              // LD HL,0xC00A; JP HL
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x3E, 0x77});
        m.steps(2);
        checkEqual(m.cpu().pc(), 0xC00A, "JP HL jumps to HL");
    }
}

void testInstructionTiming() {
    group("instruction timing");

    // These own their bytes: an initializer_list does not own its backing
    // array, so storing one in a struct that outlives the enclosing
    // expression leaves a dangling pointer.
    struct Case {
        const char* name;
        std::vector<uint8_t> setup;
        int setup_instructions;
        std::vector<uint8_t> program;
        unsigned expected;
    };

    // The cycle counts come from the bus accesses each instruction makes, so
    // these pin the accounting rather than a hand-written table.
    const std::vector<Case> cases = {
        {"NOP is 4", {}, 0, {0x00}, 4},
        {"LD A,n is 8", {}, 0, {0x3E, 0x00}, 8},
        {"LD A,B is 4", {}, 0, {0x78}, 4},
        {"LD (HL),A is 8", {0x21, 0x00, 0xD0}, 1, {0x77}, 8},
        {"LD A,(HL) is 8", {0x21, 0x00, 0xD0}, 1, {0x7E}, 8},
        {"LD (HL),n is 12", {0x21, 0x00, 0xD0}, 1, {0x36, 0x00}, 12},
        {"INC (HL) is 12", {0x21, 0x00, 0xD0}, 1, {0x34}, 12},
        {"LD BC,nn is 12", {}, 0, {0x01, 0x00, 0x00}, 12},
        {"INC BC is 8", {}, 0, {0x03}, 8},
        {"ADD HL,BC is 8", {}, 0, {0x09}, 8},
        {"JP nn is 16", {}, 0, {0xC3, 0x00, 0xC0}, 16},
        {"JP HL is 4", {}, 0, {0xE9}, 4},
        {"CALL nn is 24", {0x31, 0x00, 0xDF}, 1, {0xCD, 0x00, 0xC0}, 24},
        // The CALL target is the RET itself, so the setup lands PC on it.
        {"RET is 16", {0x31, 0x00, 0xDF, 0xCD, 0x06, 0xC0}, 2, {0xC9}, 16},
        {"PUSH BC is 16", {0x31, 0x00, 0xDF}, 1, {0xC5}, 16},
        {"POP BC is 12", {0x31, 0x00, 0xDF}, 1, {0xC1}, 12},
        {"LD (nn),A is 16", {}, 0, {0xEA, 0x00, 0xD0}, 16},
        {"LDH (n),A is 12", {}, 0, {0xE0, 0x80}, 12},
        {"ADD SP,e is 16", {}, 0, {0xE8, 0x01}, 16},
        {"LD HL,SP+e is 12", {}, 0, {0xF8, 0x01}, 12},
        {"LD SP,HL is 8", {}, 0, {0xF9}, 8},
        {"CB BIT 0,A is 8", {}, 0, {0xCB, 0x47}, 8},
        {"CB SET 0,(HL) is 16", {0x21, 0x00, 0xD0}, 1, {0xCB, 0xC6}, 16},
        {"CB BIT 0,(HL) is 12", {0x21, 0x00, 0xD0}, 1, {0xCB, 0x46}, 12},
        {"RST is 16", {0x31, 0x00, 0xDF}, 1, {0xDF}, 16},
    };

    for (const Case& item : cases) {
        Machine m;
        std::vector<uint8_t> all = item.setup;
        all.insert(all.end(), item.program.begin(), item.program.end());

        uint16_t address = Machine::kCode;
        for (uint8_t byte : all) m.memory().write(address++, byte);
        m.cpu().reset();
        m.step();   // the trampoline JP

        // Run the setup so only the instruction under test is measured.
        m.steps(item.setup_instructions);

        checkEqual(m.step(), item.expected, item.name);
    }
}

void testInterrupts() {
    group("interrupts");

    {   // A pending, enabled interrupt dispatches to its vector in 20 cycles
        Machine m;
        m.start({0x31, 0x00, 0xDF,                    // LD SP,0xDF00
                 0xFB,                                // EI
                 0x00, 0x00});                        // NOP, NOP
        m.steps(1);
        m.memory().write(0xFFFF, 0x01);               // enable vblank
        m.memory().requestInterrupt(0x01);
        m.step();                                     // EI
        // EI takes effect only after the instruction that follows it, so the
        // first NOP still runs with interrupts disabled.
        checkEqual(m.step(), 4, "the instruction after EI runs first");

        const unsigned dispatch = m.step();
        checkEqual(dispatch, 20, "interrupt dispatch costs 20 cycles");
        checkEqual(m.cpu().pc(), 0x0040, "vblank vectors to 0x0040");
        checkEqual(m.memory().read(0xFF0F) & 0x01, 0x00,
                   "the serviced flag is cleared");
    }

    {   // The return address pushed by a dispatch is the interrupted PC
        Machine m;
        m.start({0x31, 0x00, 0xDF, 0xFB, 0x00, 0x00});
        m.steps(1);
        m.memory().write(0xFFFF, 0x01);
        m.memory().requestInterrupt(0x01);
        m.steps(2);                                   // EI, then the NOP
        const uint16_t resume = m.cpu().pc();
        m.step();                                     // dispatch
        const uint16_t low = m.memory().read(m.cpu().sp());
        const uint16_t high = m.memory().read(
            static_cast<uint16_t>(m.cpu().sp() + 1));
        checkEqual(static_cast<uint16_t>((high << 8) | low), resume,
                   "the dispatch pushes the interrupted PC");
    }

    {   // With IME clear nothing is dispatched
        Machine m;
        m.start({0x31, 0x00, 0xDF, 0xF3, 0x00});      // DI; NOP
        m.steps(1);
        m.memory().write(0xFFFF, 0x01);
        m.memory().requestInterrupt(0x01);
        m.step();                                     // DI
        checkEqual(m.step(), 4, "no dispatch happens while IME is clear");
    }

    {   // HALT wakes on a pending interrupt even with IME clear
        Machine m;
        m.start({0xF3, 0x76, 0x3E, 0x55});            // DI; HALT; LD A,0x55
        m.steps(2);
        check(m.cpu().halted(), "HALT stops the CPU");
        m.memory().write(0xFFFF, 0x01);
        m.memory().requestInterrupt(0x01);
        m.step();
        check(!m.cpu().halted(), "a pending interrupt wakes HALT");
    }

    {   // A button press raises the joypad interrupt
        Machine m;
        m.memory().setButtons(0xFF);
        m.memory().write(0xFF0F, 0x00);
        m.memory().setButtons(0xFE);                  // press one key
        checkEqual(m.memory().read(0xFF0F) & 0x10, 0x10,
                   "pressing a button requests the joypad interrupt");
    }
}

void testTimer() {
    group("timer");

    {   // DIV is the top byte of a counter running at the CPU clock
        Machine m;
        m.timer().tick(255);
        checkEqual(m.memory().read(0xFF04), 0x00, "DIV holds below 256 cycles");
        m.timer().tick(1);
        checkEqual(m.memory().read(0xFF04), 0x01, "DIV ticks every 256 cycles");
    }

    {   // Writing DIV resets the whole counter
        Machine m;
        m.timer().tick(512);
        m.memory().write(0xFF04, 0x00);
        checkEqual(m.memory().read(0xFF04), 0x00, "writing DIV resets it");
    }

    {   // TIMA at the fastest rate increments every 16 cycles
        Machine m;
        m.memory().write(0xFF07, 0x05);               // enabled, 262144 Hz
        m.memory().write(0xFF05, 0x00);
        m.timer().tick(16);
        checkEqual(m.memory().read(0xFF05), 0x01, "TIMA increments at 16 cycles");
        m.timer().tick(16 * 4);
        checkEqual(m.memory().read(0xFF05), 0x05, "TIMA keeps counting");
    }

    {   // Overflow reloads from TMA and requests the timer interrupt
        Machine m;
        m.memory().write(0xFF07, 0x05);
        m.memory().write(0xFF06, 0xAB);               // TMA
        m.memory().write(0xFF05, 0xFF);               // TIMA about to overflow
        m.memory().write(0xFF0F, 0x00);
        m.timer().tick(16);
        checkEqual(m.memory().read(0xFF05), 0x00,
                   "TIMA reads zero during the reload delay");
        m.timer().tick(4);
        checkEqual(m.memory().read(0xFF05), 0xAB, "TIMA reloads from TMA");
        checkEqual(m.memory().read(0xFF0F) & 0x04, 0x04,
                   "the overflow requests the timer interrupt");
    }

    {   // A disabled timer does not count
        Machine m;
        m.memory().write(0xFF07, 0x01);               // fast rate, disabled
        m.memory().write(0xFF05, 0x00);
        m.timer().tick(1024);
        checkEqual(m.memory().read(0xFF05), 0x00, "a disabled TIMA holds");
    }
}

void testPpuTiming() {
    group("PPU timing");

    Machine m;
    m.memory().write(0xFF40, 0x80);   // LCD on

    checkEqual(m.ppu().mode(), 2, "a line starts in mode 2");
    m.ppu().tick(80);
    checkEqual(m.ppu().mode(), 3, "mode 3 begins after the OAM scan");
    m.ppu().tick(172);
    checkEqual(m.ppu().mode(), 0, "mode 0 begins after drawing");

    m.ppu().tick(204);
    checkEqual(m.ppu().line(), 1, "a line is 456 dots");

    // Advance to the start of vblank.
    m.memory().write(0xFF0F, 0x00);
    m.ppu().tick(456 * 143);
    checkEqual(m.ppu().line(), 144, "there are 144 visible lines");
    m.ppu().tick(1);
    checkEqual(m.ppu().mode(), 1, "line 144 enters vblank");
    checkEqual(m.memory().read(0xFF0F) & 0x01, 0x01,
               "entering vblank requests its interrupt");
    check(m.ppu().frameComplete(), "entering vblank completes the frame");

    m.ppu().tick(456 * 10);
    checkEqual(m.ppu().line(), 0, "the frame wraps after 154 lines");
}

void testPpuInterrupts() {
    group("PPU STAT interrupts");

    {   // LYC coincidence
        Machine m;
        m.memory().write(0xFF40, 0x80);
        m.memory().write(0xFF45, 5);         // LYC = 5
        m.memory().write(0xFF41, 0x40);      // enable the LYC source
        m.memory().write(0xFF0F, 0x00);

        m.ppu().tick(456 * 4);
        checkEqual(m.memory().read(0xFF0F) & 0x02, 0x00,
                   "no STAT interrupt before LY reaches LYC");
        m.ppu().tick(456);
        checkEqual(m.ppu().line(), 5, "LY reached LYC");
        checkEqual(m.memory().read(0xFF0F) & 0x02, 0x02,
                   "LY matching LYC requests a STAT interrupt");
        checkEqual(m.memory().read(0xFF41) & 0x04, 0x04,
                   "the coincidence flag is set");
    }

    {   // LY is read-only
        Machine m;
        m.memory().write(0xFF40, 0x80);
        m.ppu().tick(456 * 3);
        m.memory().write(0xFF44, 0x00);
        checkEqual(m.memory().read(0xFF44), 3, "writing LY is ignored");
    }

    {   // Turning the LCD off resets the line counter
        Machine m;
        m.memory().write(0xFF40, 0x80);
        m.ppu().tick(456 * 7);
        checkEqual(m.memory().read(0xFF44), 7, "LY advanced");
        m.memory().write(0xFF40, 0x00);
        checkEqual(m.memory().read(0xFF44), 0, "switching the LCD off resets LY");
    }
}

void testPpuRendering() {
    group("PPU rendering");

    // kColors from ppu.cpp, lightest first.
    constexpr uint32_t kShade[4] = {
        0xFFE0F8D0u, 0xFF88C070u, 0xFF346856u, 0xFF081820u
    };

    {   // A background tile of colour 3 fills the first line
        Machine m;
        // Tile 1 at 0x8010: every row has both bitplanes set, so colour 3.
        for (int i = 0; i < 16; ++i) m.memory().write(0x8010 + i, 0xFF);
        // Map entry (0,0) selects tile 1.
        m.memory().write(0x9800, 0x01);
        m.memory().write(0xFF47, 0xE4);   // BGP: identity-ish mapping
        m.memory().write(0xFF42, 0);
        m.memory().write(0xFF43, 0);
        m.memory().write(0xFF40, 0x91);   // LCD on, BG on, tile data at 0x8000

        m.ppu().tick(456);                // draw line 0

        const uint32_t* frame = m.ppu().framebuffer();
        checkEqual(frame[0], kShade[3], "a solid tile renders colour 3");
        checkEqual(frame[7], kShade[3], "the whole tile renders");
        checkEqual(frame[8], kShade[0], "the next tile is still blank");
    }

    {   // The palette register remaps the shades
        Machine m;
        for (int i = 0; i < 16; ++i) m.memory().write(0x8010 + i, 0xFF);
        m.memory().write(0x9800, 0x01);
        m.memory().write(0xFF47, 0x00);   // every index maps to shade 0
        m.memory().write(0xFF40, 0x91);

        m.ppu().tick(456);
        checkEqual(m.ppu().framebuffer()[0], kShade[0],
                   "BGP remaps colour 3 to shade 0");
    }

    {   // Signed tile addressing through the 0x8800 block
        Machine m;
        // With LCDC bit 4 clear, tile 0 lives at 0x9000.
        for (int i = 0; i < 16; ++i) m.memory().write(0x9000 + i, 0xFF);
        m.memory().write(0x9800, 0x00);
        m.memory().write(0xFF47, 0xE4);
        m.memory().write(0xFF40, 0x81);   // LCD on, BG on, signed tile data

        m.ppu().tick(456);
        checkEqual(m.ppu().framebuffer()[0], kShade[3],
                   "the 0x8800 block addresses tiles as signed offsets");
    }

    {   // A sprite draws over the background
        Machine m;
        // Tile 1 is the sprite; tile 0, which the background map selects by
        // default, is left blank so the background is colour 0 everywhere.
        for (int i = 0; i < 16; ++i) m.memory().write(0x8010 + i, 0xFF);
        m.memory().write(0xFF47, 0xE4);
        m.memory().write(0xFF48, 0xE4);   // OBP0
        // Sprite 0 at screen (0,0) using tile 1.
        m.memory().write(0xFE00, 16);     // Y = 16 maps to line 0
        m.memory().write(0xFE01, 8);      // X = 8 maps to column 0
        m.memory().write(0xFE02, 0x01);
        m.memory().write(0xFE03, 0x00);
        m.memory().write(0xFF40, 0x93);   // LCD, BG and sprites on

        m.ppu().tick(456);
        checkEqual(m.ppu().framebuffer()[0], kShade[3],
                   "a sprite renders over the background");
        checkEqual(m.ppu().framebuffer()[8], kShade[0],
                   "the sprite is only eight pixels wide");
    }

    {   // A sprite with the priority bit hides behind a non-zero background
        Machine m;
        for (int i = 0; i < 16; ++i) m.memory().write(0x8010 + i, 0xFF);
        m.memory().write(0x9800, 0x01);   // background colour 3 everywhere
        for (int i = 0; i < 16; ++i) m.memory().write(0x8000 + i, 0x00);
        m.memory().write(0x8000, 0xFF);   // sprite row 0 is colour 1
        m.memory().write(0xFF47, 0xE4);
        m.memory().write(0xFF48, 0x00);   // OBP0 maps everything to shade 0
        m.memory().write(0xFE00, 16);
        m.memory().write(0xFE01, 8);
        m.memory().write(0xFE02, 0x00);
        m.memory().write(0xFE03, 0x80);   // behind the background
        m.memory().write(0xFF40, 0x93);

        m.ppu().tick(456);
        checkEqual(m.ppu().framebuffer()[0], kShade[3],
                   "a low-priority sprite stays behind the background");
    }

    {   // The window covers the background where it starts
        Machine m;
        for (int i = 0; i < 16; ++i) m.memory().write(0x8010 + i, 0xFF);
        for (int i = 0; i < 16; ++i) m.memory().write(0x8020 + i, 0x00);
        m.memory().write(0x9800, 0x01);   // background tile 1, colour 3
        m.memory().write(0x9C00, 0x02);   // window tile 2, colour 0
        m.memory().write(0xFF47, 0xE4);
        m.memory().write(0xFF4A, 0);      // WY = 0
        m.memory().write(0xFF4B, 7 + 8);  // WX so the window starts at x = 8
        m.memory().write(0xFF40, 0xF1);   // LCD, BG, window on, window map 9C00

        m.ppu().tick(456);
        checkEqual(m.ppu().framebuffer()[0], kShade[3],
                   "the background shows left of the window");
        checkEqual(m.ppu().framebuffer()[8], kShade[0],
                   "the window covers the background from WX on");
    }

    {   // OAM DMA copies 160 bytes
        Machine m;
        for (int i = 0; i < 160; ++i) {
            m.memory().write(static_cast<uint16_t>(0xD000 + i),
                             static_cast<uint8_t>(i));
        }
        m.memory().write(0xFF46, 0xD0);
        checkEqual(m.memory().read(0xFE00), 0x00, "OAM DMA copies the first byte");
        checkEqual(m.memory().read(0xFE9F), 159, "OAM DMA copies the last byte");
    }
}

void testCartridge() {
    group("cartridge banking");

    {   // MBC1 selects a ROM bank, and bank 0 maps to 1
        Machine m;
        m.memory().write(0x2000, 0x00);
        // Bank 0 is unreachable in the high half, so this reads bank 1.
        const uint8_t bank_one = m.memory().read(0x4000);
        m.memory().write(0x2000, 0x01);
        checkEqual(m.memory().read(0x4000), bank_one,
                   "writing bank 0 selects bank 1");
    }

    {   // Cartridge RAM needs enabling before it answers
        Machine m;
        m.memory().write(0x0000, 0x00);               // RAM disabled
        m.memory().write(0xA000, 0x5A);
        checkEqual(m.memory().read(0xA000), 0xFF,
                   "disabled cartridge RAM reads as 0xFF");

        m.memory().write(0x0000, 0x0A);               // RAM enabled
        m.memory().write(0xA000, 0x5A);
        checkEqual(m.memory().read(0xA000), 0x5A,
                   "enabled cartridge RAM stores a byte");
    }
}

void testSerial() {
    group("serial output");

    // Test ROMs report their results here, so the bytes have to be captured.
    Machine m;
    m.memory().write(0xFF01, 'O');
    m.memory().write(0xFF02, 0x81);
    m.memory().write(0xFF01, 'K');
    m.memory().write(0xFF02, 0x81);
    checkEqual(m.memory().serialOutput() == "OK", 1,
               "serial writes are collected");
}

} // namespace

int main() {
    std::printf("Game Boy core\n");

    testLoadsAndAlu();
    testSixteenBit();
    testRotatesAndBits();
    testControlFlow();
    testInstructionTiming();
    testInterrupts();
    testTimer();
    testPpuTiming();
    testPpuInterrupts();
    testPpuRendering();
    testCartridge();
    testSerial();

    std::error_code ignored;
    std::filesystem::remove(trampolineRom(), ignored);

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
