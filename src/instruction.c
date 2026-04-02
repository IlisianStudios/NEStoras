#include <stdint.h>

#include "../include/bus.h"
#include "../include/cpu.h"
#include "../include/instruction.h"

static inline void stack_push(CPU *cpu, uint8_t value)
{
    bus_write(0x0100 | cpu->sp, value);
    cpu->sp--;
}

static inline uint8_t stack_pop(CPU *cpu)
{
    cpu->sp++;
    return bus_read(0x0100 | cpu->sp);
}

uint8_t op_LDA(CPU *cpu) {
    cpu->a = bus_read(cpu->pc);
    set_flag(cpu, FLAG_Z, cpu->a);
    set_flag(cpu, FLAG_N, cpu->a);
    return 1; // This instruction allows extra cycle
}

uint8_t op_LDX(CPU *cpu) {
    cpu->x = bus_read(cpu->pc);
    set_flag(cpu, FLAG_Z, cpu->a);
    set_flag(cpu, FLAG_N, cpu->a);
    return 1; // This instruction allows extra cycle
}

uint8_t op_STA(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->a);
    return 0;
}

uint8_t op_STX(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->x);
    return 0;
}

uint8_t op_STY(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->y);
    return 0;
}

uint8_t op_TAX(CPU *cpu) {
    cpu->x = cpu->a;
    update_nz(cpu, cpu->x);
    return 0;
}

uint8_t op_TAY(CPU *cpu) {
    cpu->y = cpu->a;
    update_nz(cpu, cpu->y);
    return 0;
}

uint8_t op_TXA(CPU *cpu) {
    cpu->a = cpu->x;
    update_nz(cpu, cpu->a);
    return 0;
}

uint8_t op_TYA(CPU *cpu) {
    cpu->a = cpu->y;
    update_nz(cpu, cpu->a);
    return 0;
}

uint8_t op_TSX(CPU *cpu) {
    cpu->x = cpu->sp;
    update_nz(cpu, cpu->x);
    return 0;
}

uint8_t op_TXS(CPU *cpu) {
    cpu->sp = cpu->x;
    return 0;
}

uint8_t op_PHA(CPU *cpu) {
    stack_push(cpu, cpu->a);
    return 0;
}

uint8_t op_PLA(CPU *cpu) {
    cpu->a = stack_pop(cpu);
    update_nz(cpu, cpu->a);
    return 0;
}

uint8_t op_PHP(CPU *cpu) {
    stack_push(cpu, cpu->status | FLAG_B | FLAG_U);
    return 0;
}

uint8_t op_PLP(CPU *cpu) {
    cpu->status = stack_pop(cpu);
    set_flag(cpu, FLAG_U, true);
    set_flag(cpu, FLAG_B, false);
    return 0;
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

//https://masswerk.at/6502/6502_instruction_set.html#LSR
void init_lookup() {
    for (int i = 0; i < 256; i++) {
        lookup[i] = (instruction){
            "???", op_NOOP, addr_IMP, 1, 2
        };
    }
    // NOP
    lookup[0xEA] = (instruction){ "NOP", op_NOOP, addr_IMP, 1, 2 };
    // LDA
    lookup[0xA9] = (instruction){ "LDA", op_LDA, addr_IMM, 2, 2 };
    lookup[0xA5] = (instruction){ "LDA", op_LDA, addr_ZPO, 2, 3 };
    lookup[0xB5] = (instruction){ "LDA", op_LDA, addr_ZPX, 2, 4 };
    lookup[0xAD] = (instruction){ "LDA", op_LDA, addr_ABS, 2, 4 };
    lookup[0xBD] = (instruction){ "LDA", op_LDA, addr_ABX, 2, 4 };
    lookup[0xB9] = (instruction){ "LDA", op_LDA, addr_ABY, 2, 4 };
    lookup[0xA1] = (instruction){ "LDA", op_LDA, addr_IDX, 2, 6 };
    lookup[0xB1] = (instruction){ "LDA", op_LDA, addr_IZY, 2, 5 };
    // LDX
    lookup[0xA2] = (instruction){ "LDX", op_LDX, addr_IMM, 2, 2 };
    lookup[0xA6] = (instruction){ "LDX", op_LDX, addr_ZPO, 2, 3 };
    lookup[0xB6] = (instruction){ "LDX", op_LDX, addr_ZPY, 2, 4 };
    lookup[0xAE] = (instruction){ "LDX", op_LDX, addr_ABS, 2, 4 };
    lookup[0xBE] = (instruction){ "LDX", op_LDX, addr_ABY, 2, 4 };
    // LDY
    lookup[0xA0] = (instruction){ "LDY", op_LDY, addr_IMM, 2, 2 };
    lookup[0xA4] = (instruction){ "LDY", op_LDY, addr_ZPO, 2, 3 };
    lookup[0xB4] = (instruction){ "LDY", op_LDY, addr_ZPX, 2, 4 };
    lookup[0xAC] = (instruction){ "LDY", op_LDY, addr_ABS, 2, 4 };
    lookup[0xBC] = (instruction){ "LDY", op_LDY, addr_ABX, 2, 4 };
    // STA
    lookup[0x85] = (instruction){ "STA", op_STA, addr_ZPO, 2, 3 };
    lookup[0x95] = (instruction){ "STA", op_STA, addr_ZPX, 2, 4 };
    lookup[0x8D] = (instruction){ "STA", op_STA, addr_ABS, 3, 4 };
    lookup[0x9D] = (instruction){ "STA", op_STA, addr_ABX, 3, 5 };
    lookup[0x99] = (instruction){ "STA", op_STA, addr_ABY, 3, 5 };
    lookup[0x81] = (instruction){ "STA", op_STA, addr_IDX, 2, 6 };
    lookup[0x91] = (instruction){ "STA", op_STA, addr_IZY, 2, 6 };
    // STX
    lookup[0x85] = (instruction){ "STX", op_STX, addr_ZPO, 2, 3 };
    lookup[0x95] = (instruction){ "STX", op_STX, addr_ZPY, 2, 4 };
    lookup[0x8D] = (instruction){ "STX", op_STX, addr_ABS, 3, 4 };
    // STY
    lookup[0x85] = (instruction){ "STY", op_STY, addr_ZPO, 2, 3 };
    lookup[0x95] = (instruction){ "STY", op_STY, addr_ZPY, 2, 4 };
    lookup[0x8D] = (instruction){ "STY", op_STY, addr_ABS, 3, 4 };
    // TAX
    lookup[0xAA] = (instruction){ "TAX", op_TAX, addr_IMP, 1, 2 };
    // TAY
    lookup[0xA8] = (instruction){ "TAY", op_TAY, addr_IMP, 1, 2 };
    // TXA
    lookup[0x8A] = (instruction){ "TXA", op_TXA, addr_IMP, 1, 2 };
    // TYA
    lookup[0x98] = (instruction){ "TYA", op_TYA, addr_IMP, 1, 2 };
    // TSX
    lookup[0xBA] = (instruction){ "TSX", op_TSX, addr_IMP, 1, 2 };
    // TXS
    lookup[0x9A] = (instruction){ "TXS", op_TXS, addr_IMP, 1, 2 };
    // PHA
    lookup[0x48] = (instruction){ "PHA", op_PHA, addr_IMP, 1, 3 };
    // PLA
    lookup[0x68] = (instruction){ "PLA", op_PLA, addr_IMP, 1, 4 };
    // PHP
    lookup[0x08] = (instruction){ "PHP", op_PHP, addr_IMP, 1, 3 };
    // PLP
    lookup[0x28] = (instruction){ "PLP", op_PLP, addr_IMP, 1, 4 };
}


