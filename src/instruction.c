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

uint8_t op_NOOP(CPU *cpu) {
    return 0;
}

uint8_t addr_IMP(CPU *cpu) {
    cpu->fetched = cpu->a;
    return 0;
}

uint8_t addr_IMM(CPU *cpu) {
    cpu->addr_abs = advance_pc(cpu);
    return 0;
}

uint8_t addr_ZPO(CPU *cpu) {
    cpu->addr_abs = bus_read(advance_pc(cpu));
    cpu->addr_abs &= 0xFF;
    return 0;
}

uint8_t addr_ZPX(CPU *cpu) {
    uint8_t const addr = bus_read(advance_pc(cpu));
    cpu->addr_abs = (addr + cpu->x) & 0x00FF;
    return 0;
}

uint8_t addr_ZPY(CPU *cpu) {
    uint8_t const addr = bus_read(advance_pc(cpu));
    cpu->addr_abs = (addr + cpu->y) & 0x00FF;
    return 0;
}

uint8_t addr_REL(CPU *cpu) {
    cpu->addr_rel = bus_read(advance_pc(cpu));

    if (cpu->addr_rel == 0x80) {
        cpu->addr_rel |= 0xFF00;
    }
    return 0;
}

uint8_t read_word(CPU *cpu) {
    const uint8_t lo = bus_read(advance_pc(cpu));
    const uint8_t hi = bus_read(advance_pc(cpu));
    return (hi << 8) | lo;
}

uint8_t addr_ABS(CPU *cpu) {
    cpu->addr_abs = read_word(cpu);
    return 0;
}

uint8_t addr_ABX(CPU *cpu) {
    uint16_t const base =  read_word(cpu);
    cpu->addr_abs = base +  + cpu->x;

    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00);
}

uint8_t addr_ABY(CPU *cpu) {
    uint16_t const base =  read_word(cpu);
    cpu->addr_abs = base +  + cpu->y;

    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00);
}

uint8_t addr_IDX(CPU *cpu) {
    const uint8_t addr = bus_read(advance_pc(cpu));
    const uint8_t ptr = (addr + cpu->x) & 0xFF;
    const uint8_t lo = bus_read(ptr);
    const uint8_t hi = bus_read(ptr+1) & 0xFF;
    cpu->addr_abs = (hi << 8) | lo;

    return 0;
}

uint8_t addr_IND(CPU *cpu) {
    const uint16_t ptr_lo = bus_read(cpu->pc++);
    const uint16_t ptr_hi = bus_read(cpu->pc++);

    const uint16_t ptr = (ptr_hi << 8) | ptr_lo;

    if (ptr_lo == 0xFF){
        const uint16_t lo = bus_read(ptr);
        const uint16_t hi = bus_read(ptr & 0xFF00);
        cpu->addr_abs = (hi << 8) | lo;
    }
    else {
        const uint16_t lo = bus_read(ptr);
        const uint16_t hi = bus_read(ptr + 1);
        cpu->addr_abs = (hi << 8) | lo;
    }
    return 0;
}

uint8_t addr_IZY(CPU *cpu) {
    uint8_t const ptr = bus_read(advance_pc(cpu));

    const uint16_t lo = bus_read(ptr);
    const uint16_t hi = bus_read((uint8_t)(ptr + 1));

    uint16_t base = (hi << 8) | lo;
    cpu->addr_abs = base + cpu->y;

    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00);
}

instruction lookup[256];

