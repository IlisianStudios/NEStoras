#pragma once
#include <SDL_stdinc.h>
#include <stdint.h>
#include <stdbool.h>
#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif



// Flag bit positions
#define FLAG_C 0x01  // Carry
#define FLAG_Z 0x02  // Zero
#define FLAG_I 0x04  // Interrupt disable
#define FLAG_D 0x08  // Decimal (ignored on NES, but the bit exists)
#define FLAG_B 0x10  // Break
#define FLAG_U 0x20  // Unused (always 1)
#define FLAG_V 0x40  // Overflow
#define FLAG_N 0x80  // Negative

typedef struct{
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp; // stack pointer $0100-$01FF
    uint16_t pc;
    uint8_t status;

    uint8_t cycles;
    uint64_t total_cycles;

    uint16_t addr_abs;
    uint16_t addr_rel;
    uint8_t fetched;

    bool testing_mode;
    bool nestest_comp;
} CPU;

void cpu_reset(CPU *cpu);
void cpu_nmi(CPU *cpu);
void cpu_irq(CPU *cpu);
void cpu_step(CPU *cpu);
void set_flag(CPU *cpu, uint8_t flag, bool value);
bool get_flag(const CPU *cpu, uint8_t flag);
void update_nz(CPU *cpu, uint8_t value);

uint16_t static inline advance_pc(CPU *cpu){
    const uint16_t mem = cpu->pc;
    cpu->pc++;
    return mem;
}

static inline void stack_push(CPU *cpu, uint8_t value){
    bus_write(0x0100 | cpu->sp, value);
    cpu->sp--;
}

static inline uint8_t stack_pop(CPU *cpu){
    cpu->sp++;
    return bus_read(0x0100 | cpu->sp);
}

void audio_callback(void *userdata, Uint8 *stream, int len);


#ifdef __cplusplus
}
#endif

