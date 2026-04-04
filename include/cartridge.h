#pragma once
#ifndef NESTORAS_CARTRIGE_H
#define NESTORAS_CARTRIGE_H
#include <stdbool.h>
#include <stdint.h>

#endif //NESTORAS_CARTRIGE_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Cartridge Cartridge;

typedef struct {
    uint8_t (*cpu_read)(Cartridge*, uint16_t addr);
    void (*cpu_write)(Cartridge*, uint16_t addr, uint8_t data);
    uint8_t (*ppu_read)(Cartridge*, uint16_t addr);
    void (*ppu_write)(Cartridge*, uint16_t addr, uint8_t data);
} Mapper;

struct Cartridge{
    uint8_t *prg_rom;
    uint8_t *chr_rom;
    uint32_t prg_size;
    uint32_t chr_size;
    uint8_t mapper_id;
    uint8_t mirroring;

    Mapper mapper;
};

extern Cartridge *cartridge;

bool cartridge_load(const char* path);
void cartridge_free();
void try_free_cartridge();

#ifdef __cplusplus
}
#endif