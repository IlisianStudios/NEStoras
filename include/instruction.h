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
void init_lookup();

uint8_t op_LDA(CPU *cpu);
uint8_t op_LDX(CPU *cpu);
uint8_t op_LDY(CPU *cpu);
uint8_t op_STA(CPU *cpu);
uint8_t op_STX(CPU *cpu);
uint8_t op_STY(CPU *cpu);

uint8_t op_TAX(CPU *cpu);
uint8_t op_TAY(CPU *cpu);
uint8_t op_TXA(CPU *cpu);
uint8_t op_TYA(CPU *cpu);
uint8_t op_TSX(CPU *cpu);
uint8_t op_TXS(CPU *cpu);

uint8_t op_PHA(CPU *cpu);
uint8_t op_PLA(CPU *cpu);
uint8_t op_PHP(CPU *cpu);
uint8_t op_PLP(CPU *cpu);

uint8_t op_ADC(CPU *cpu);
uint8_t op_SBC(CPU *cpu);
uint8_t op_AND(CPU *cpu);
uint8_t op_ORA(CPU *cpu);
uint8_t op_CMP(CPU *cpu);
uint8_t op_CPX(CPU *cpu);
uint8_t op_CPY(CPU *cpu);
uint8_t op_EOR(CPU *cpu);

uint8_t op_BCC(CPU *cpu);
uint8_t op_BCS(CPU *cpu);
uint8_t op_BEQ(CPU *cpu);
uint8_t op_BNE(CPU *cpu);
uint8_t op_BMI(CPU *cpu);
uint8_t op_BPL(CPU *cpu);
uint8_t op_BVC(CPU *cpu);
uint8_t op_BVS(CPU *cpu);

uint8_t op_ASL(CPU *cpu);
uint8_t op_LSR(CPU *cpu);
uint8_t op_ROL(CPU *cpu);
uint8_t op_ROR(CPU *cpu);

uint8_t op_JMP(CPU *cpu);
uint8_t op_JSR(CPU *cpu);
uint8_t op_RTS(CPU *cpu);
uint8_t op_RTI(CPU *cpu);
uint8_t op_BRK(CPU *cpu);
uint8_t op_INC(CPU *cpu);
uint8_t op_DEC(CPU *cpu);

uint8_t op_CLC(CPU *cpu);
uint8_t op_SEC(CPU *cpu);
uint8_t op_CLI(CPU *cpu);
uint8_t op_SEI(CPU *cpu);
uint8_t op_CLV(CPU *cpu);
uint8_t op_CLD(CPU *cpu);
uint8_t op_SED(CPU *cpu);

uint8_t op_BIT(CPU *cpu);

uint8_t op_NOOP(CPU *cpu);

uint8_t addr_IMP(CPU *cpu);
uint8_t addr_ACC(CPU *cpu);
uint8_t addr_IMM(CPU *cpu);
uint8_t addr_ZPO(CPU *cpu);
uint8_t addr_ZPX(CPU *cpu);
uint8_t addr_ZPY(CPU *cpu);
uint8_t addr_REL(CPU *cpu);
uint8_t addr_ABS(CPU *cpu);
uint8_t addr_ABX(CPU *cpu);
uint8_t addr_ABY(CPU *cpu);
uint8_t addr_IDX(CPU *cpu);
uint8_t addr_IND(CPU *cpu);
uint8_t addr_IZY(CPU *cpu);

#ifdef __cplusplus
}
#endif