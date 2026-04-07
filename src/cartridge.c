#include "cartridge.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <mappers.h>

#include "cpu.h"

Cartridge *cartridge = NULL;

void try_free_cartridge(void) {
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

    cartridge = (Cartridge *)calloc(1, sizeof(Cartridge));

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

    // PAL detection: iNES header byte 9 bit 0, plus filename heuristic
    cartridge->is_pal = (header[9] & 0x01) != 0;
    // Filename heuristic: "(Europe)" or "(PAL)" in path
    if (!cartridge->is_pal) {
        if (strstr(path, "Europe") || strstr(path, "europe") ||
            strstr(path, "(PAL)")  || strstr(path, "(pal)")) {
            cartridge->is_pal = true;
        }
    }
    printf("ROM: PRG=%uKB CHR=%uKB mapper=%u %s\n",
           cartridge->prg_size / 1024, cartridge->chr_size / 1024,
           cartridge->mapper_id, cartridge->is_pal ? "PAL" : "NTSC");

    // skip trainer, no idea what a trainer is in this context
    if (header[6] & 0x04) fseek(f, 512, SEEK_CUR);

    cartridge->prg_rom = malloc(cartridge->prg_size);
    if (!cartridge->prg_rom) {
        fclose(f);
        free(cartridge);
        cartridge = NULL;
        return false;
    }

    size_t chr_alloc = cartridge->chr_size == 0 ? 8192 : cartridge->chr_size;
    cartridge->chr_rom = malloc(chr_alloc);
    if (!cartridge->chr_rom) {
        fclose(f);
        free(cartridge->prg_rom);
        free(cartridge);
        cartridge = NULL;
        return false;
    }

    add_mapper(cartridge);
    fread(cartridge->prg_rom, 1, cartridge->prg_size, f);
    fread(cartridge->chr_rom, 1, cartridge->chr_size, f);

    fclose(f);
    return true;

}

void cartridge_free(void) {
    if (!cartridge) return;
    free(cartridge->prg_rom);
    free(cartridge->chr_rom);
    free(cartridge);
}
