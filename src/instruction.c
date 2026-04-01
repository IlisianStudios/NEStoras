#include <stdint.h>

#include "../include/bus.h"
#include "../include/cpu.h"
#include "../include/instruction.h"

uint8_t op_LDA(CPU *cpu) {
    cpu->a = bus_read(cpu->pc);
    set_flag(cpu, FLAG_Z, cpu->a);
    set_flag(cpu, FLAG_N, cpu->a);
    return 1; // This instruction allows extra cycle
}

uint8_t add_IMM(CPU *cpu) {
    return cpu->pc++;
}

instruction lookup[256];

