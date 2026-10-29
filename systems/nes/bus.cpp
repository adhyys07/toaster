#include "bus.h"

#include <cstring>

namespace nes {

Bus::Bus() {
    std::memset(ram_, 0, sizeof(ram_));
}

void Bus::insertCartridge(Cartridge* c) {
    cart = c;
    ppu.connectCartridge(c);
}

void Bus::reset() {
    if (cart) cart->reset();
    ppu.reset();
    apu.reset();
    cpu.reset();          
    clock_counter_ = 0;
    dma_active_ = false;
    dma_sync_   = true;
    dma_page_ = dma_addr_ = dma_data_ = 0;
    std::memset(ram_, 0, sizeof(ram_));
}

uint8_t Bus::cpuRead(uint16_t addr, bool readonly) {
    uint8_t data = 0;

    if (cart && cart->cpuRead(addr, data)) return data;

    if (addr <= 0x1FFF) {
        return ram_[addr & 0x07FF];
    }
    if (addr <= 0x3FFF) {
        return ppu.cpuRead(addr & 0x0007, readonly);
    }
    if (addr == 0x4015) {
        return apu.cpuRead(addr);
    }
    if (addr == 0x4016 || addr == 0x4017) {
        const int port = addr & 0x0001;
        data = (controller_shift_[port] & 0x80) ? 1 : 0;
        if (!readonly) {
            controller_shift_[port] = static_cast<uint8_t>(controller_shift_[port] << 1);
        }
        return data;
    }
    return 0;  // open bus, approximated as zero
}

void Bus::cpuWrite(uint16_t addr, uint8_t data) {
    if (cart && cart->cpuWrite(addr, data)) return;

    if (addr <= 0x1FFF) {
        ram_[addr & 0x07FF] = data;
        return;
    }
    if (addr <= 0x3FFF) {
        ppu.cpuWrite(addr & 0x0007, data);
        return;
    }
    if (addr == 0x4014) {
        dma_page_   = data;
        dma_addr_   = 0;
        dma_active_ = true;
        dma_sync_   = true;
        return;
    }
    if (addr == 0x4016) {
        controller_shift_[0] = controller_[0];
        controller_shift_[1] = controller_[1];
        return;
    }
    if ((addr >= 0x4000 && addr <= 0x4013) || addr == 0x4015 || addr == 0x4017) {
        apu.cpuWrite(addr, data);
        return;
    }
}

void Bus::clock() {
    ppu.clock();

    if (clock_counter_ % 3 == 0) {
        if (dma_active_) {
            if (dma_sync_) {
                if (clock_counter_ % 6 == 3) dma_sync_ = false;
            } else {
                if (clock_counter_ % 6 == 0) {
                    dma_data_ = cpuRead(static_cast<uint16_t>((dma_page_ << 8) | dma_addr_));
                } else {
                    ppu.oamBytes()[dma_addr_] = dma_data_;
                    dma_addr_++;
                    if (dma_addr_ == 0) {  // wrapped past 255: all 256 bytes done
                        dma_active_ = false;
                        dma_sync_   = true;
                    }
                }
            }
        } else {
            cpu.clock();
        }
        apu.clock();
    }

    if (ppu.nmi) {
        ppu.nmi = false;
        cpu.nmi();
    }

    if (apu.irq) {
        cpu.irq();
    }

    clock_counter_++;
}

void Bus::clockFrame() {
    do { clock(); } while (!ppu.frame_complete);
    ppu.frame_complete = false;
}

}  // namespace nes