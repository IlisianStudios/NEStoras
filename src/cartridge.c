#include "cartridge.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <mappers.h>

#include "cpu.h"

Cartridge *cartridge = NULL;

void try_free_cartridge() {
    if (cartridge != NULL) {
        cartridge_free();
        cartridge = NULL;
    }
}

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

bool cartridge_load(const char* path) {
    try_free_cartridge();
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cartridge_load: can't open %s\n", path);
        return false;
    }

    cartridge =  (Cartridge *)malloc(sizeof(Cartridge));

    uint8_t header[16];
    fread(header, 1, 16, f);

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S'
        || header[3] != 0x1A) {
            fclose(f);
            free(cartridge);
            cartridge = NULL;
            return false;
        }

    cartridge->prg_size = header[4] * 16384;
    cartridge->chr_size = header[5] * 8192;
    cartridge->mapper_id = (header[7] & 0xF0) | (header[6] >> 4);
    cartridge->mirroring = header[6] & 0x01;

    // skip trainer, no idea what a trainer is in this context
    if (header[6] & 0x04) fseek(f, 512, SEEK_CUR);

    cartridge->prg_rom = malloc(cartridge->prg_size);

    if (cartridge->chr_size == 0)
        cartridge->chr_rom = malloc(8192);
    else
        cartridge->chr_rom = malloc(cartridge->chr_size);

    add_mapper(cartridge);
    fread(cartridge->prg_rom, 1, cartridge->prg_size, f);
    fread(cartridge->chr_rom, 1, cartridge->chr_size, f);

    fclose(f);
    return true;

}

void cartridge_free() {
    free(cartridge->prg_rom);
    free(cartridge->chr_rom);

    cartridge->prg_rom = NULL;
    cartridge->chr_rom = NULL;
}
