#include <stdint.h>

#include "../include/bus.h"
#include "../include/cpu.h"
#include "../include/instruction.h"

static inline uint8_t branch(CPU *cpu, const bool cond)
{
    if (cond)
    {
        cpu->cycles++;

        const uint16_t addr = cpu->pc + cpu->addr_rel;

        if ((addr & 0xFF00) != (cpu->pc & 0xFF00))
            cpu->cycles++;

        cpu->pc = addr;
    }

    return 0;
}

static inline uint16_t sr_helper_read(const CPU *cpu, const bool cond) {
    if (cond)
        return  cpu->a;
    return bus_read(cpu->addr_abs);
}

static inline void sr_helper_write(CPU *cpu, const bool cond, uint8_t data) {
    if (cond) {
        cpu->a = data;
    } else {
        bus_write(cpu->addr_abs, data);
    }
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

uint8_t op_LDY(CPU *cpu) {
    cpu->y = bus_read(cpu->pc);
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

uint8_t op_ADC(CPU *cpu) {
    uint16_t const sum = cpu->a + cpu->addr_abs + get_flag(cpu, FLAG_C);

    set_flag(cpu, FLAG_C, sum & 0xFF);
    set_flag(cpu, FLAG_V, (~(cpu->a ^ cpu->addr_abs) & (cpu->a ^ sum)) & 0x80);
    cpu->a = sum & 0xFF;
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_SBC(CPU *cpu) {
    cpu->addr_abs = ~cpu->addr_abs;
    return op_ADC(cpu);
}

uint8_t op_AND(CPU *cpu) {
    cpu->a &= cpu->addr_abs;
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_ORA(CPU *cpu) {
    cpu->a |= cpu->addr_abs;
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_CMP(CPU *cpu) {
    const uint8_t result = cpu->a - cpu->addr_abs;
    set_flag(cpu, FLAG_C, result & 0xFF);
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_CPX(CPU *cpu) {
    const uint8_t result = cpu->x - cpu->addr_abs;
    set_flag(cpu, FLAG_C, result & 0xFF);
    update_nz(cpu, cpu->x);
    return 0;
}

uint8_t op_CPY(CPU *cpu) {
    const uint8_t result = cpu->y- cpu->addr_abs;
    set_flag(cpu, FLAG_C, result & 0xFF);
    update_nz(cpu, cpu->y);
    return 0;
}

uint8_t op_EOR(CPU *cpu) {
    cpu->a ^= cpu->addr_abs;
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_BCC(CPU *cpu) {
    return branch(cpu, !get_flag(cpu, FLAG_C));
}

uint8_t op_BCS(CPU *cpu) {
    return branch(cpu, get_flag(cpu, FLAG_C));
}

uint8_t op_BEQ(CPU *cpu) {
    return branch(cpu, get_flag(cpu, FLAG_Z));
}

uint8_t op_BNE(CPU *cpu) {
    return branch(cpu, !get_flag(cpu, FLAG_Z));
}

uint8_t op_BMI(CPU *cpu) {
    return branch(cpu, get_flag(cpu, FLAG_N));
}

uint8_t op_BPL(CPU *cpu) {
    return branch(cpu, !get_flag(cpu, FLAG_N));
}

uint8_t op_BVC(CPU *cpu) {
    return branch(cpu, !get_flag(cpu, FLAG_V));
}

uint8_t op_BVS(CPU *cpu) {
    return branch(cpu, get_flag(cpu, FLAG_V));
}

uint8_t op_ASL(CPU *cpu) {

    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    set_flag(cpu, FLAG_C, temp & 0xFF00);
    temp = temp << 1;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_LSR(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    set_flag(cpu, FLAG_C, temp & 0x01);
    temp = temp >> 1;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_ROL(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    uint8_t const old_c = get_flag(cpu, FLAG_C);
    set_flag(cpu, FLAG_C, temp & 0x80);

    temp = (temp << 1) | old_c;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_ROR(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    uint8_t old_c = get_flag(cpu, FLAG_C);
    set_flag(cpu, FLAG_C, temp & 0x01);

    old_c = old_c << 7;
    temp = (temp >> 1) | old_c;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_JMP(CPU *cpu) {
    cpu->a = cpu->addr_abs;
    return 0;
}

uint8_t op_JSR(CPU *cpu) {
    cpu->pc--;

    stack_push(cpu, (cpu->pc >> 8) & 0xFF);
    stack_push(cpu, (cpu->pc & 0xFF));

    cpu->pc = cpu->addr_abs;
    return 0;
}

uint8_t op_RTS(CPU *cpu) {
    const uint8_t lo = stack_pop(cpu);
    const uint8_t hi = stack_pop(cpu);

    cpu->pc = (hi << 8) | lo;
    cpu->pc++;
    return 0;
}

uint8_t op_RTI(CPU *cpu) {
    cpu->status = stack_pop(cpu);

    const uint16_t lo = stack_pop(cpu);
    const uint16_t hi = stack_pop(cpu);

    cpu->pc = (hi << 8) | lo;
    return 0;
}

uint8_t op_BRK(CPU *cpu) {
    cpu->pc++;

    stack_push(cpu, (cpu->pc >> 8) & 0xFF);
    stack_push(cpu, cpu->pc & 0xFF);

    set_flag(cpu, FLAG_B, true);
    stack_push(cpu, cpu->status);
    set_flag(cpu, FLAG_I, true);

    uint16_t lo = bus_read(0xFFFE);
    uint16_t hi = bus_read(0xFFFF);

    cpu->pc = (hi << 8) | lo;
    return 0;
}

uint8_t op_INC(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    const uint16_t v = sr_helper_read(cpu, cond) + 1;
    sr_helper_write(cpu, cond, v);
    update_nz(cpu, v);

    return 0;
}

uint8_t op_DEC(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == addr_ACC;
    const uint16_t v = sr_helper_read(cpu, cond) - 1;
    sr_helper_write(cpu, cond, v);
    update_nz(cpu, v);

    return 0;
}

uint8_t op_CLC(CPU *cpu) {
    set_flag(cpu, FLAG_C, false);
    return 0;
}

uint8_t op_SEC(CPU *cpu) {
    set_flag(cpu, FLAG_C, true);
    return 0;
}

uint8_t op_CLI(CPU *cpu) {
    set_flag(cpu, FLAG_I, false);
    return 0;
}

uint8_t op_SEI(CPU *cpu) {
    set_flag(cpu, FLAG_I, true);
    return 0;
}

uint8_t op_CLV(CPU *cpu) {
    set_flag(cpu, FLAG_V, false);
    return 0;
}

uint8_t op_CLD(CPU *cpu) {
    set_flag(cpu, FLAG_D, false);
    return 0;
}

uint8_t op_SED(CPU *cpu) {
    set_flag(cpu, FLAG_D, true);
    return 0;
}

uint8_t op_BIT(CPU *cpu) {
    const uint8_t value = bus_read(cpu->addr_abs);

    const uint8_t result = cpu->a & value;

    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_N, value & 0x80);
    set_flag(cpu, FLAG_V, value & 0x40);

    return 0;
}

uint8_t op_NOOP(CPU *cpu) {
    return 0;
}

uint8_t addr_IMP(CPU *cpu) {
    cpu->fetched = cpu->a;
    return 0;
}

uint8_t addr_ACC(CPU *cpu) {
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
    // ALU
    lookup[0x72] = (instruction){ "ADC", op_ADC, addr_ZPO, 2, 5 };
    lookup[0xF2] = (instruction){ "SBC", op_SBC, addr_ZPO, 2, 5 };

    lookup[0x29] = (instruction){ "AND", op_AND, addr_IMM, 2, 2 };
    lookup[0x25] = (instruction){ "AND", op_AND, addr_ZPO, 2, 3 };
    lookup[0x35] = (instruction){ "AND", op_AND, addr_ZPX, 2, 4 };
    lookup[0x3D] = (instruction){ "AND", op_AND, addr_ABX, 3, 4 };
    lookup[0x2D] = (instruction){ "AND", op_AND, addr_ABS, 3, 4 };
    lookup[0x39] = (instruction){ "AND", op_AND, addr_ABY, 3, 4 };
    lookup[0x21] = (instruction){ "AND", op_AND, addr_IDX, 2, 6 };
    lookup[0x31] = (instruction){ "AND", op_AND, addr_IZY, 2, 5 };

    lookup[0x09] = (instruction){ "ORA", op_ORA, addr_IMM, 2, 2 };
    lookup[0x05] = (instruction){ "ORA", op_ORA, addr_ZPO, 2, 3 };
    lookup[0x15] = (instruction){ "ORA", op_ORA, addr_ZPX, 2, 4 };
    lookup[0x0D] = (instruction){ "ORA", op_ORA, addr_ABS, 3, 4 };
    lookup[0x1D] = (instruction){ "ORA", op_ORA, addr_ABX, 3, 4 };
    lookup[0x19] = (instruction){ "ORA", op_ORA, addr_ABY, 3, 4 };
    lookup[0x01] = (instruction){ "ORA", op_ORA, addr_IDX, 2, 6 };
    lookup[0x11] = (instruction){ "ORA", op_ORA, addr_IZY, 2, 5 };

    lookup[0x49] = (instruction){ "EOR", op_EOR, addr_IMM, 2, 2 };
    lookup[0x45] = (instruction){ "EOR", op_EOR, addr_ZPO, 2, 3 };
    lookup[0x55] = (instruction){ "EOR", op_EOR, addr_ZPX, 2, 4 };
    lookup[0x4D] = (instruction){ "EOR", op_EOR, addr_ABS, 3, 4 };
    lookup[0x5D] = (instruction){ "EOR", op_EOR, addr_ABX, 3, 4 };
    lookup[0x59] = (instruction){ "EOR", op_EOR, addr_ABY, 3, 4 };
    lookup[0x41] = (instruction){ "EOR", op_EOR, addr_IDX, 2, 6 };
    lookup[0x51] = (instruction){ "EOR", op_EOR, addr_IZY, 2, 5 };

    lookup[0xD2] = (instruction){ "CMP", op_CMP, addr_ZPO, 2, 5 };

    lookup[0xE0] = (instruction){ "CPX", op_CPX, addr_IMM, 2, 2 };
    lookup[0xE4] = (instruction){ "CPX", op_CPX, addr_ZPO, 2, 3 };
    lookup[0xEC] = (instruction){ "CPX", op_CPX, addr_ABS, 3, 4 };

    lookup[0xC0] = (instruction){ "CPY", op_CPY, addr_IMM, 2, 2 };
    lookup[0xC4] = (instruction){ "CPY", op_CPY, addr_ZPO, 2, 3 };
    lookup[0xCC] = (instruction){ "CPY", op_CPY, addr_ABS, 3, 4 };
    // Branching
    lookup[0x90] = (instruction){ "BCC", op_BCC, addr_REL, 2, 2 };
    lookup[0xB0] = (instruction){ "BCS", op_BCS, addr_REL, 2, 2 };
    lookup[0xF0] = (instruction){ "BEQ", op_BEQ, addr_REL, 2, 2 };
    lookup[0xD0] = (instruction){ "BNE", op_BNE, addr_REL, 2, 2 };
    lookup[0x30] = (instruction){ "BMI", op_BMI, addr_REL, 2, 2 };
    lookup[0x10] = (instruction){ "BPL", op_BPL, addr_REL, 2, 2 };
    lookup[0x50] = (instruction){ "BVC", op_BVC, addr_REL, 2, 2 };
    lookup[0x70] = (instruction){ "BVS", op_BVS, addr_REL, 2, 2 };
    // SHIFTS AND ROTATES
    lookup[0x0A] = (instruction){ "ASL", op_ASL, addr_ACC, 1, 2 };
    lookup[0x06] = (instruction){ "ASL", op_ASL, addr_ZPO, 2, 5 };
    lookup[0x16] = (instruction){ "ASL", op_ASL, addr_ZPX, 2, 6 };
    lookup[0x0E] = (instruction){ "ASL", op_ASL, addr_ABS, 3, 6 };
    lookup[0x1E] = (instruction){ "ASL", op_ASL, addr_ABX, 3, 7 };

    lookup[0x4A] = (instruction){ "LSR", op_LSR, addr_ACC, 1, 2 };
    lookup[0x46] = (instruction){ "LSR", op_LSR, addr_ZPO, 2, 5 };
    lookup[0x56] = (instruction){ "LSR", op_LSR, addr_ZPX, 2, 6 };
    lookup[0x4E] = (instruction){ "LSR", op_LSR, addr_ABS, 3, 6 };
    lookup[0x5E] = (instruction){ "LSR", op_LSR, addr_ABX, 3, 7 };

    lookup[0x2A] = (instruction){ "ROL", op_ROL, addr_ACC, 1, 2 };
    lookup[0x26] = (instruction){ "ROL", op_ROL, addr_ZPO, 2, 5 };
    lookup[0x36] = (instruction){ "ROL", op_ROL, addr_ZPX, 2, 6 };
    lookup[0x2E] = (instruction){ "ROL", op_ROL, addr_ABS, 3, 6 };
    lookup[0x3E] = (instruction){ "ROL", op_ROL, addr_ABX, 3, 7 };

    lookup[0x6A] = (instruction){ "ROR", op_ROR, addr_ACC, 1, 2 };
    lookup[0x66] = (instruction){ "ROR", op_ROR, addr_ZPO, 2, 5 };
    lookup[0x76] = (instruction){ "ROR", op_ROR, addr_ZPX, 2, 6 };
    lookup[0x6E] = (instruction){ "ROR", op_ROR, addr_ABS, 3, 6 };
    lookup[0x7E] = (instruction){ "ROR", op_ROR, addr_ABX, 3, 7 };
    // CONtROL
    lookup[0x4C] = (instruction){ "JMP", op_JMP, addr_ABS, 3, 3 };
    lookup[0x6C] = (instruction){ "JMP", op_JMP, addr_IND, 3, 5 };

    lookup[0x20] = (instruction){ "JSR", op_JSR, addr_ABS, 3, 6 };
    lookup[0x60] = (instruction){ "RTS", op_RTS, addr_IMP, 1, 6 };
    lookup[0x40] = (instruction){ "RTI", op_RTI, addr_IMP, 1, 6 };
    lookup[0x00] = (instruction){ "BRK", op_BRK, addr_IMP, 1, 7 };

    lookup[0x1A] = (instruction){ "INC", op_INC, addr_ACC, 1, 2 };
    lookup[0xE6] = (instruction){ "INC", op_INC, addr_ZPO, 2, 5 };
    lookup[0xF6] = (instruction){ "INC", op_INC, addr_ZPX, 2, 6 };
    lookup[0xEE] = (instruction){ "INC", op_INC, addr_ABS, 3, 6 };
    lookup[0xFE] = (instruction){ "INC", op_INC, addr_ABX, 3, 7 };

    lookup[0x3A] = (instruction){ "DEC", op_DEC, addr_ACC, 1, 2 };
    lookup[0xC6] = (instruction){ "DEC", op_DEC, addr_ZPO, 2, 5 };
    lookup[0xD6] = (instruction){ "DEC", op_DEC, addr_ZPX, 2, 6 };
    lookup[0xCE] = (instruction){ "DEC", op_DEC, addr_ABS, 3, 6 };
    lookup[0xDE] = (instruction){ "DEC", op_DEC, addr_ABX, 3, 7 };

    lookup[0x18] = (instruction){ "CLC", op_CLC, addr_IMP, 1, 2 };
    lookup[0x38] = (instruction){ "SEC", op_SEC, addr_IMP, 1, 2 };
    lookup[0x58] = (instruction){ "CLI", op_CLI, addr_IMP, 1, 2 };
    lookup[0x71] = (instruction){ "SEI", op_SEI, addr_IMP, 1, 2 };
    lookup[0xB8] = (instruction){ "CLV", op_CLV, addr_IMP, 1, 2 };
    lookup[0xD8] = (instruction){ "CLD", op_CLD, addr_IMP, 1, 2 };
    lookup[0xF8] = (instruction){ "SED", op_SED, addr_IMP, 1, 2 };

    lookup[0x89] = (instruction){ "BIT", op_BIT, addr_IMM, 2, 2 };
    lookup[0x24] = (instruction){ "BIT", op_BIT, addr_ZPO, 2, 3 };
    lookup[0x34] = (instruction){ "BIT", op_BIT, addr_ZPX, 2, 4 };
    lookup[0x2C] = (instruction){ "BIT", op_BIT, addr_ABS, 3, 4 };
    lookup[0x3C] = (instruction){ "BIT", op_BIT, addr_ABX, 3, 4 };






}


