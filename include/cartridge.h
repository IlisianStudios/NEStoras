#ifndef NESTORAS_CARTRIGE_H
#define NESTORAS_CARTRIGE_H
#include <stdbool.h>
#include <stdint.h>

#endif //NESTORAS_CARTRIGE_H

typedef struct {
    uint8_t *prg_rom;
    uint8_t *chr_rom;
    uint32_t prg_size;
    uint32_t chr_size;
    uint8_t mapper;
    uint8_t mirroring;
} Cartridge;

bool cartridge_load(Cartridge *cartrige, const char* path);
void cartridge_free(Cartridge *cart);