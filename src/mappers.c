#include "mappers.h"

uint8_t nrom_cpu_read(Cartridge *cart, uint16_t addr) {
    if (addr >= 0x8000) {
        if (cart->prg_size == 0x4000)
            return cart->prg_rom[(addr - 0x8000) % 0x4000];
        else
            return cart->prg_rom[addr - 0x8000];
    }
    return 0;
}

void nrom_cpu_write(Cartridge *cart, uint16_t addr, uint8_t data) {
    (void)cart; (void)addr; (void)data;
    // NROM is ROM-only, writing has no effect
}

uint8_t nrom_ppu_read(Cartridge *cart, uint16_t addr) {
    return cart->chr_rom[addr];
}

void nrom_ppu_write(Cartridge *cart, uint16_t addr, uint8_t data) {
    (void)cart; (void)addr; (void)data;
    // NROM CHR ROM is usually read-only
}

