#pragma once

#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "ppu.h"

namespace nes {


enum Button : uint8_t {
    BTN_A      = 0x80,
    BTN_B      = 0x40,
    BTN_SELECT = 0x20,
    BTN_START  = 0x10,
    BTN_UP     = 0x08,
    BTN_DOWN   = 0x04,
    BTN_LEFT   = 0x02,
    BTN_RIGHT  = 0x01,
};

class Bus {
public:
    Bus();

    void insertCartridge(Cartridge* cart);
    void reset();
    void clock();       
    void clockFrame();   

    uint8_t cpuRead(uint16_t addr, bool readonly = false);
    void    cpuWrite(uint16_t addr, uint8_t data);

    void setController(int port, uint8_t state) { controller_[port & 1] = state; }

    float audioSample() const { return apu.output(); }

    CPU cpu{*this};
    PPU ppu;
    APU apu;
    Cartridge* cart = nullptr;

    uint64_t masterClock() const { return clock_counter_; }

private:
    uint8_t ram_[2048] = {};

    uint8_t controller_[2]       = {};
    uint8_t controller_shift_[2] = {};  

    uint64_t clock_counter_ = 0;

    bool    dma_active_ = false;
    bool    dma_sync_   = true;  
    uint8_t dma_page_   = 0;
    uint8_t dma_addr_   = 0;
    uint8_t dma_data_   = 0;
};

}  