//
// Created by Pandora on 3/4/26.
//

#pragma once
#ifndef NESTORAS_MAPPERS_H
#define NESTORAS_MAPPERS_H

#include <stdint.h>
#include "cartridge.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t nrom_cpu_read(Cartridge *cart, uint16_t addr);
void nrom_cpu_write(Cartridge *cart, uint16_t addr, uint8_t data);
uint8_t nrom_ppu_read(Cartridge *cart, uint16_t addr);
void nrom_ppu_write(Cartridge *cart, uint16_t addr, uint8_t data);

#ifdef __cplusplus
}
#endif

#endif //NESTORAS_MAPPERS_H
