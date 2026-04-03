#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t ram[2048];

static inline uint8_t apu_io_read(uint16_t addr) {
    (void) addr;
    return 0x00;
}

static inline uint8_t ppu_register_read(uint16_t addr) {
    (void) addr;
    return 0x00;
}

static inline uint8_t cartrige_read(uint16_t addr) {
    (void) addr;
    return 0x00;
}

static inline void apu_io_write(uint16_t addr, uint8_t data) {}

static inline uint8_t ppu_register_write(uint16_t addr, uint8_t data) {}

static inline uint8_t cartrige_write(uint16_t addr, uint8_t data) {}

static inline uint8_t bus_read(const uint16_t addr) {
    /*In the 2A03 memory map, you’ll notice the RAM is only 2 KB ($0000–$07FF), but the map says it goes up to $1FFF.
    This happens because the NES hardware is "lazy" with its wiring to save money. The chip only looks at the bottom few wires of the address. As a result:
    Address $0000 is the real RAM.
    Address $0800 points to the exact same physical spot.
    Address $1000 points there too.
    */
    if (addr < 0x2000) return ram[addr & 0x07ff];
    else if (addr < 0x4000) return ppu_register_read(0x2000 | (addr & 0x2007));
    else if (addr < 0x4020) return apu_io_read(addr);
    else return cartrige_read(addr);
}

static inline void bus_write(const uint16_t addr, const uint8_t data) {
    if (addr < 0x2000) {
        ram[addr & 0x07ff] = data;
    }
    else if (addr < 0x4000) {
        ppu_register_write(0x2000 | (addr & 0x2007), data);
    }
    else if (addr < 0x4020) {
        apu_io_write(addr, data);
    }
    cartrige_write(addr, data);
}

#ifdef __cplusplus
}
#endif