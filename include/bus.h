#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "cartridge.h"
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t ram[2048];
extern bool ppu_nmi_enable;

// Controller state — bits: A B Select Start Up Down Left Right
extern uint8_t controller_state[2];
extern uint8_t controller_shift[2];
extern bool controller_strobe;

extern uint8_t apu_read(uint16_t addr);
extern void apu_write(uint16_t addr, uint8_t data);
extern uint32_t apu_get_frame_cycles(void);

// PPU timing thresholds (set by apu_init based on PAL/NTSC)
extern uint32_t ppu_vblank_end;
extern uint32_t ppu_vblank_start;
extern uint32_t ppu_sp0_hit_start;
extern uint32_t ppu_sp0_hit_end;

static inline uint8_t apu_io_read(uint16_t addr) {
    if (addr == 0x4015) return apu_read(addr);
    if (addr == 0x4016 || addr == 0x4017) {
        uint8_t idx = addr & 1;  // 0 for $4016, 1 for $4017
        if (controller_strobe) {
            // While strobe is high, always return current state of button A (bit 7)
            return (controller_state[idx] >> 7) & 1;
        }
        // Return top bit of shift register, then shift left
        // NES controller outputs A first (bit 7), then B (bit 6), ..., Right (bit 0)
        uint8_t val = (controller_shift[idx] >> 7) & 1;
        controller_shift[idx] <<= 1;
        // After 8 reads, subsequent reads return 1 (open bus behavior)
        controller_shift[idx] |= 1;
        return val;
    }
    return 0;
}

static inline void apu_io_write(uint16_t addr, uint8_t data) {
    if (addr == 0x4016) {
        bool new_strobe = data & 1;
        if (controller_strobe && !new_strobe) {
            // Strobe going low → latch controller state into shift registers
            controller_shift[0] = controller_state[0];
            controller_shift[1] = controller_state[1];
        }
        controller_strobe = new_strobe;
        return;
    }
    apu_write(addr, data);
}

static inline uint8_t ppu_register_read(uint16_t addr) {
    if (addr == 0x2002) {
        uint8_t status = 0;
        uint32_t fc = apu_get_frame_cycles();

        // Vblank flag: set during vblank periods (thresholds adjusted for PAL/NTSC)
        if (fc < ppu_vblank_end || fc >= ppu_vblank_start)
            status |= 0x80;

        // Sprite 0 hit (thresholds adjusted for PAL/NTSC)
        if (fc >= ppu_sp0_hit_start && fc < ppu_sp0_hit_end)
            status |= 0x40;

        return status;
    }
    return 0x00;
}

static inline void ppu_register_write(uint16_t addr, uint8_t data) {
    if (addr == 0x2000) {
        ppu_nmi_enable = (data >> 7) & 1;
    }
}

static inline void cartrige_write(uint16_t addr, uint8_t data) {
    (void)addr; (void)data;
}

static inline uint8_t bus_read(const uint16_t addr) {
    /*In the 2A03 memory map, you’ll notice the RAM is only 2 KB ($0000–$07FF), but the map says it goes up to $1FFF.
    This happens because the NES hardware is "lazy" with its wiring to save money. The chip only looks at the bottom few wires of the address. As a result:
    Address $0000 is the real RAM.
    Address $0800 points to the exact same physical spot.
    Address $1000 points there too.
    */
    if (addr < 0x2000) return ram[addr & 0x07ff];
    else if (addr < 0x4000) return ppu_register_read(addr & 0x2007 );
    else if (addr < 0x4020) return apu_io_read(addr);
    else if (cartridge && cartridge->mapper.cpu_read) return cartridge->mapper.cpu_read(cartridge, addr);
    else return 0;
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
    else if (cartridge && cartridge->mapper.cpu_write) {
        cartridge->mapper.cpu_write(cartridge, addr, data);
    }
}


#ifdef __cplusplus
}
#endif

