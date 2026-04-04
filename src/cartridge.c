#include "cartridge.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <mappers.h>

#include "cpu.h"

Cartridge *cartridge = NULL;

void add_mapper(Cartridge *cart) {
    switch(cart->mapper_id) {
        case 0: {
            // NROM
            cart->mapper.cpu_read  = nrom_cpu_read;
            cart->mapper.cpu_write = nrom_cpu_write;
            cart->mapper.ppu_read  = nrom_ppu_read;
            cart->mapper.ppu_write = nrom_ppu_write;
            break;
        }
        default:break;
    }
}

bool cartridge_load(Cartridge *cart, const char* path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t header[16];
    fread(header, 1, 16, f);

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S'
        || header[3] != 0x1A) {
            fclose(f);
            return false;
        }

    cart->prg_size = header[4] * 16384;
    cart->chr_size = header[5] * 8192;
    cart->mapper_id = (header[7] & 0xF0) | (header[6] >> 4);
    cart->mirroring = header[6] & 0x01;

    // skip trainer, no idea what a trainer is in this context
    if (header[6] & 0x04) fseek(f, 512, SEEK_CUR);

    cart->prg_rom = malloc(cart->prg_size);
    cart->chr_rom = malloc(cart->chr_size);

    if (cart->chr_size == 0)
        cart->chr_rom = (uint8_t*)malloc(8192);
    else {
        cart->chr_rom = malloc(cart->chr_size);
    }

    add_mapper(cart);
    fread(cart->prg_rom, 1, cart->prg_size, f);
    fread(cart->chr_rom, 1, cart->chr_size, f);

    fclose(f);
    return true;

}

void cartridge_free(Cartridge *cart) {
    free(cart->prg_rom);
    free(cart->chr_rom);

    cart->prg_rom = NULL;
    cart->chr_rom = NULL;
}
