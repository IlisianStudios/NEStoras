//
// Created by Pandora on 1/4/26.
//
#pragma once
#include <stdint.h>

#include "cpu.h"

#ifndef NESTORAS_INSTRUCTION_H
#define NESTORAS_INSTRUCTION_H

#endif //NESTORAS_INSTRUCTION_H
#ifdef __cplusplus
extern "C" {
#endif

// Addressing mode returns 1 if page boundary crossed
typedef uint8_t (*addrmode_f)(CPU *cpu);

// Operation returns 1 if extra cycle may be needed
typedef uint8_t (*operatemode_f)(CPU *cpu);

typedef struct {
    const char *name;
    operatemode_f operate;
    addrmode_f addrmode;
    uint8_t bytes;
    uint8_t cycles;
} instruction;

extern instruction lookup[256];

uint8_t op_LDA(CPU *cpu);
uint8_t op_NOOP(CPU *cpu);

uint8_t addr_IMP(CPU *cpu);
uint8_t addr_IMM(CPU *cpu);
uint8_t addr_ZPO(CPU *cpu);
uint8_t addr_ZPX(CPU *cpu);
uint8_t addr_ZPY(CPU *cpu);
uint8_t addr_REL(CPU *cpu);
uint8_t addr_ABS(CPU *cpu);
uint8_t addr_ABSX(CPU *cpu);
uint8_t addr_ABSY(CPU *cpu);
uint8_t addr_IDX(CPU *cpu);
uint8_t addr_IND(CPU *cpu);
uint8_t addr_IZY(CPU *cpu);

#ifdef __cplusplus
}
#endif