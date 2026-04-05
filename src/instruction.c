#include "instruction.h"

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
    cpu->a = bus_read(cpu->addr_abs);
    update_nz(cpu, cpu->a);
    return 1; // This instruction allows extra cycle
}

uint8_t op_LDX(CPU *cpu) {
    cpu->x = bus_read(cpu->addr_abs);
    update_nz(cpu, cpu->x);
    return 1; // This instruction allows extra cycle
}

uint8_t op_LDY(CPU *cpu) {
    cpu->y = bus_read(cpu->addr_abs);
    update_nz(cpu, cpu->y);
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

static inline uint8_t adc_impl(CPU *cpu, uint8_t value) {
    const uint16_t sum = cpu->a + value + get_flag(cpu, FLAG_C);

    set_flag(cpu, FLAG_C, sum > 0xFF);
    set_flag(cpu, FLAG_V, (~(cpu->a ^ value) & (cpu->a ^ sum)) & 0x80);
    cpu->a = sum & 0xFF;
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_ADC(CPU *cpu) {
    return adc_impl(cpu, bus_read(cpu->addr_abs));
}

uint8_t op_SBC(CPU *cpu) {
    return adc_impl(cpu, ~bus_read(cpu->addr_abs));
}

uint8_t op_AND(CPU *cpu) {
    cpu->a &= bus_read(cpu->addr_abs);
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_ORA(CPU *cpu) {
    cpu->a |= bus_read(cpu->addr_abs);
    update_nz(cpu, cpu->a);
    return 1;
}

uint8_t op_CMP(CPU *cpu) {
    const uint8_t value = bus_read(cpu->addr_abs);
    const uint8_t result = cpu->a - value;
    set_flag(cpu, FLAG_C, cpu->a >= value);
    update_nz(cpu, result);
    return 1;
}

uint8_t op_CPX(CPU *cpu) {
    const uint8_t value = bus_read(cpu->addr_abs);
    const uint8_t result = cpu->x - value;
    set_flag(cpu, FLAG_C, cpu->x >= value);
    update_nz(cpu, result);
    return 0;
}

uint8_t op_CPY(CPU *cpu) {
    const uint8_t value = bus_read(cpu->addr_abs);
    const uint8_t result = cpu->y - value;
    set_flag(cpu, FLAG_C, cpu->y >= value);
    update_nz(cpu, result);
    return 0;
}

uint8_t op_EOR(CPU *cpu) {
    cpu->a ^= bus_read(cpu->addr_abs);
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

    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    set_flag(cpu, FLAG_C, temp & 0x80);
    temp = temp << 1;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_LSR(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    set_flag(cpu, FLAG_C, temp & 0x01);
    temp = temp >> 1;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_ROL(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
    uint16_t temp = sr_helper_read(cpu, cond);

    uint8_t const old_c = get_flag(cpu, FLAG_C);
    set_flag(cpu, FLAG_C, temp & 0x80);

    temp = (temp << 1) | old_c;
    update_nz(cpu, temp & 0xFF);

    sr_helper_write(cpu, cond, temp);
    return 0;
}

uint8_t op_ROR(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
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
    cpu->pc = cpu->addr_abs;
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

    set_flag(cpu, FLAG_U, true);
    set_flag(cpu, FLAG_B, false);

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
    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
    const uint16_t v = sr_helper_read(cpu, cond) + 1;
    sr_helper_write(cpu, cond, v);
    update_nz(cpu, v);

    return 0;
}

uint8_t op_INY(CPU *cpu) {
    cpu->y += 1;
    update_nz(cpu, cpu->y);
    return 0;
}

uint8_t op_INX(CPU *cpu) {
    cpu->x += 1;
    update_nz(cpu, cpu->x);
    return 0;
}

uint8_t op_DEC(CPU *cpu) {
    const bool cond = lookup[cpu->fetched].addrmode == &addr_ACC;
    const uint16_t v = sr_helper_read(cpu, cond) - 1;
    sr_helper_write(cpu, cond, v);
    update_nz(cpu, v);

    return 0;
}

uint8_t op_DEX(CPU *cpu) {
    cpu->x -= 1;
    update_nz(cpu, cpu->x);
    return 0;
}

uint8_t op_DEY(CPU *cpu) {
    cpu->y -= 1;
    update_nz(cpu, cpu->y);
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

// Note: cpu could be const here, but signature must match operatemode_f typedef
uint8_t op_NOOP(CPU *cpu) {
    (void)cpu;
    return 1;
}

uint8_t addr_IMP(CPU *cpu) {
    (void)cpu;
    return 0;
}

uint8_t addr_ACC(CPU *cpu) {
    (void)cpu;
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

    if (cpu->addr_rel & 0x80) {
        cpu->addr_rel |= 0xFF00;
    }
    return 0;
}

uint16_t read_word(CPU *cpu) {
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
    const uint8_t hi = bus_read((ptr+1) & 0xFF);
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

Instruction lookup[256];

//https://masswerk.at/6502/6502_instruction_set.html#LSR
void init_lookup(void) {
    for (int i = 0; i < 256; i++) {
        lookup[i] = (Instruction){
            "???", op_NOOP, addr_IMP, 1, 2
        };
    }
    // NOP
    lookup[0xEA] = (Instruction){ "NOP", op_NOOP, addr_IMP, 1, 2 };
    // LDA
    lookup[0xA9] = (Instruction){ "LDA", op_LDA, addr_IMM, 2, 2 };
    lookup[0xA5] = (Instruction){ "LDA", op_LDA, addr_ZPO, 2, 3 };
    lookup[0xB5] = (Instruction){ "LDA", op_LDA, addr_ZPX, 2, 4 };
    lookup[0xAD] = (Instruction){ "LDA", op_LDA, addr_ABS, 2, 4 };
    lookup[0xBD] = (Instruction){ "LDA", op_LDA, addr_ABX, 2, 4 };
    lookup[0xB9] = (Instruction){ "LDA", op_LDA, addr_ABY, 2, 4 };
    lookup[0xA1] = (Instruction){ "LDA", op_LDA, addr_IDX, 2, 6 };
    lookup[0xB1] = (Instruction){ "LDA", op_LDA, addr_IZY, 2, 5 };
    // LDX
    lookup[0xA2] = (Instruction){ "LDX", op_LDX, addr_IMM, 2, 2 };
    lookup[0xA6] = (Instruction){ "LDX", op_LDX, addr_ZPO, 2, 3 };
    lookup[0xB6] = (Instruction){ "LDX", op_LDX, addr_ZPY, 2, 4 };
    lookup[0xAE] = (Instruction){ "LDX", op_LDX, addr_ABS, 2, 4 };
    lookup[0xBE] = (Instruction){ "LDX", op_LDX, addr_ABY, 2, 4 };
    // LDY
    lookup[0xA0] = (Instruction){ "LDY", op_LDY, addr_IMM, 2, 2 };
    lookup[0xA4] = (Instruction){ "LDY", op_LDY, addr_ZPO, 2, 3 };
    lookup[0xB4] = (Instruction){ "LDY", op_LDY, addr_ZPX, 2, 4 };
    lookup[0xAC] = (Instruction){ "LDY", op_LDY, addr_ABS, 2, 4 };
    lookup[0xBC] = (Instruction){ "LDY", op_LDY, addr_ABX, 2, 4 };
    // STA
    lookup[0x85] = (Instruction){ "STA", op_STA, addr_ZPO, 2, 3 };
    lookup[0x95] = (Instruction){ "STA", op_STA, addr_ZPX, 2, 4 };
    lookup[0x8D] = (Instruction){ "STA", op_STA, addr_ABS, 3, 4 };
    lookup[0x9D] = (Instruction){ "STA", op_STA, addr_ABX, 3, 5 };
    lookup[0x99] = (Instruction){ "STA", op_STA, addr_ABY, 3, 5 };
    lookup[0x81] = (Instruction){ "STA", op_STA, addr_IDX, 2, 6 };
    lookup[0x91] = (Instruction){ "STA", op_STA, addr_IZY, 2, 6 };
    // STX
    lookup[0x86] = (Instruction){ "STX", op_STX, addr_ZPO, 2, 3 };
    lookup[0x96] = (Instruction){ "STX", op_STX, addr_ZPY, 2, 4 };
    lookup[0x8E] = (Instruction){ "STX", op_STX, addr_ABS, 3, 4 };
    // STY
    lookup[0x84] = (Instruction){ "STY", op_STY, addr_ZPO, 2, 3 };
    lookup[0x94] = (Instruction){ "STY", op_STY, addr_ZPX, 2, 4 };
    lookup[0x8C] = (Instruction){ "STY", op_STY, addr_ABS, 3, 4 };
    // TAX
    lookup[0xAA] = (Instruction){ "TAX", op_TAX, addr_IMP, 1, 2 };
    // TAY
    lookup[0xA8] = (Instruction){ "TAY", op_TAY, addr_IMP, 1, 2 };
    // TXA
    lookup[0x8A] = (Instruction){ "TXA", op_TXA, addr_IMP, 1, 2 };
    // TYA
    lookup[0x98] = (Instruction){ "TYA", op_TYA, addr_IMP, 1, 2 };
    // TSX
    lookup[0xBA] = (Instruction){ "TSX", op_TSX, addr_IMP, 1, 2 };
    // TXS
    lookup[0x9A] = (Instruction){ "TXS", op_TXS, addr_IMP, 1, 2 };
    // PHA
    lookup[0x48] = (Instruction){ "PHA", op_PHA, addr_IMP, 1, 3 };
    // PLA
    lookup[0x68] = (Instruction){ "PLA", op_PLA, addr_IMP, 1, 4 };
    // PHP
    lookup[0x08] = (Instruction){ "PHP", op_PHP, addr_IMP, 1, 3 };
    // PLP
    lookup[0x28] = (Instruction){ "PLP", op_PLP, addr_IMP, 1, 4 };
    // ALU
    lookup[0x69] = (Instruction){ "ADC", op_ADC, addr_IMM, 2, 2 };
    lookup[0x65] = (Instruction){ "ADC", op_ADC, addr_ZPO, 2, 3 };
    lookup[0x75] = (Instruction){ "ADC", op_ADC, addr_ZPX, 2, 4 };
    lookup[0x6D] = (Instruction){ "ADC", op_ADC, addr_ABS, 3, 4 };
    lookup[0x7D] = (Instruction){ "ADC", op_ADC, addr_ABX, 3, 4 };
    lookup[0x79] = (Instruction){ "ADC", op_ADC, addr_ABY, 3, 4 };
    lookup[0x61] = (Instruction){ "ADC", op_ADC, addr_IDX, 2, 6 };
    lookup[0x71] = (Instruction){ "ADC", op_ADC, addr_IZY, 2, 5 };

    lookup[0xE9] = (Instruction){ "SBC", op_SBC, addr_IMM, 2, 2 };
    lookup[0xE5] = (Instruction){ "SBC", op_SBC, addr_ZPO, 2, 3 };
    lookup[0xF5] = (Instruction){ "SBC", op_SBC, addr_ZPX, 2, 4 };
    lookup[0xED] = (Instruction){ "SBC", op_SBC, addr_ABS, 3, 4 };
    lookup[0xFD] = (Instruction){ "SBC", op_SBC, addr_ABX, 3, 4 };
    lookup[0xF9] = (Instruction){ "SBC", op_SBC, addr_ABY, 3, 4 };
    lookup[0xE1] = (Instruction){ "SBC", op_SBC, addr_IDX, 2, 6 };
    lookup[0xF1] = (Instruction){ "SBC", op_SBC, addr_IZY, 2, 5 };

    lookup[0x29] = (Instruction){ "AND", op_AND, addr_IMM, 2, 2 };
    lookup[0x25] = (Instruction){ "AND", op_AND, addr_ZPO, 2, 3 };
    lookup[0x35] = (Instruction){ "AND", op_AND, addr_ZPX, 2, 4 };
    lookup[0x3D] = (Instruction){ "AND", op_AND, addr_ABX, 3, 4 };
    lookup[0x2D] = (Instruction){ "AND", op_AND, addr_ABS, 3, 4 };
    lookup[0x39] = (Instruction){ "AND", op_AND, addr_ABY, 3, 4 };
    lookup[0x21] = (Instruction){ "AND", op_AND, addr_IDX, 2, 6 };
    lookup[0x31] = (Instruction){ "AND", op_AND, addr_IZY, 2, 5 };

    lookup[0x09] = (Instruction){ "ORA", op_ORA, addr_IMM, 2, 2 };
    lookup[0x05] = (Instruction){ "ORA", op_ORA, addr_ZPO, 2, 3 };
    lookup[0x15] = (Instruction){ "ORA", op_ORA, addr_ZPX, 2, 4 };
    lookup[0x0D] = (Instruction){ "ORA", op_ORA, addr_ABS, 3, 4 };
    lookup[0x1D] = (Instruction){ "ORA", op_ORA, addr_ABX, 3, 4 };
    lookup[0x19] = (Instruction){ "ORA", op_ORA, addr_ABY, 3, 4 };
    lookup[0x01] = (Instruction){ "ORA", op_ORA, addr_IDX, 2, 6 };
    lookup[0x11] = (Instruction){ "ORA", op_ORA, addr_IZY, 2, 5 };

    lookup[0x49] = (Instruction){ "EOR", op_EOR, addr_IMM, 2, 2 };
    lookup[0x45] = (Instruction){ "EOR", op_EOR, addr_ZPO, 2, 3 };
    lookup[0x55] = (Instruction){ "EOR", op_EOR, addr_ZPX, 2, 4 };
    lookup[0x4D] = (Instruction){ "EOR", op_EOR, addr_ABS, 3, 4 };
    lookup[0x5D] = (Instruction){ "EOR", op_EOR, addr_ABX, 3, 4 };
    lookup[0x59] = (Instruction){ "EOR", op_EOR, addr_ABY, 3, 4 };
    lookup[0x41] = (Instruction){ "EOR", op_EOR, addr_IDX, 2, 6 };
    lookup[0x51] = (Instruction){ "EOR", op_EOR, addr_IZY, 2, 5 };

    lookup[0xC9] = (Instruction){ "CMP", op_CMP, addr_IMM, 2, 2 };
    lookup[0xC5] = (Instruction){ "CMP", op_CMP, addr_ZPO, 2, 3 };
    lookup[0xD5] = (Instruction){ "CMP", op_CMP, addr_ZPX, 2, 4 };
    lookup[0xCD] = (Instruction){ "CMP", op_CMP, addr_ABS, 3, 4 };
    lookup[0xDD] = (Instruction){ "CMP", op_CMP, addr_ABX, 3, 4 };
    lookup[0xD9] = (Instruction){ "CMP", op_CMP, addr_ABY, 3, 4 };
    lookup[0xC1] = (Instruction){ "CMP", op_CMP, addr_IDX, 2, 6 };
    lookup[0xD1] = (Instruction){ "CMP", op_CMP, addr_IZY, 2, 5 };

    lookup[0xE0] = (Instruction){ "CPX", op_CPX, addr_IMM, 2, 2 };
    lookup[0xE4] = (Instruction){ "CPX", op_CPX, addr_ZPO, 2, 3 };
    lookup[0xEC] = (Instruction){ "CPX", op_CPX, addr_ABS, 3, 4 };

    lookup[0xC0] = (Instruction){ "CPY", op_CPY, addr_IMM, 2, 2 };
    lookup[0xC4] = (Instruction){ "CPY", op_CPY, addr_ZPO, 2, 3 };
    lookup[0xCC] = (Instruction){ "CPY", op_CPY, addr_ABS, 3, 4 };
    // Branching
    lookup[0x90] = (Instruction){ "BCC", op_BCC, addr_REL, 2, 2 };
    lookup[0xB0] = (Instruction){ "BCS", op_BCS, addr_REL, 2, 2 };
    lookup[0xF0] = (Instruction){ "BEQ", op_BEQ, addr_REL, 2, 2 };
    lookup[0xD0] = (Instruction){ "BNE", op_BNE, addr_REL, 2, 2 };
    lookup[0x30] = (Instruction){ "BMI", op_BMI, addr_REL, 2, 2 };
    lookup[0x10] = (Instruction){ "BPL", op_BPL, addr_REL, 2, 2 };
    lookup[0x50] = (Instruction){ "BVC", op_BVC, addr_REL, 2, 2 };
    lookup[0x70] = (Instruction){ "BVS", op_BVS, addr_REL, 2, 2 };
    // SHIFTS AND ROTATES
    lookup[0x0A] = (Instruction){ "ASL", op_ASL, addr_ACC, 1, 2 };
    lookup[0x06] = (Instruction){ "ASL", op_ASL, addr_ZPO, 2, 5 };
    lookup[0x16] = (Instruction){ "ASL", op_ASL, addr_ZPX, 2, 6 };
    lookup[0x0E] = (Instruction){ "ASL", op_ASL, addr_ABS, 3, 6 };
    lookup[0x1E] = (Instruction){ "ASL", op_ASL, addr_ABX, 3, 7 };

    lookup[0x4A] = (Instruction){ "LSR", op_LSR, addr_ACC, 1, 2 };
    lookup[0x46] = (Instruction){ "LSR", op_LSR, addr_ZPO, 2, 5 };
    lookup[0x56] = (Instruction){ "LSR", op_LSR, addr_ZPX, 2, 6 };
    lookup[0x4E] = (Instruction){ "LSR", op_LSR, addr_ABS, 3, 6 };
    lookup[0x5E] = (Instruction){ "LSR", op_LSR, addr_ABX, 3, 7 };

    lookup[0x2A] = (Instruction){ "ROL", op_ROL, addr_ACC, 1, 2 };
    lookup[0x26] = (Instruction){ "ROL", op_ROL, addr_ZPO, 2, 5 };
    lookup[0x36] = (Instruction){ "ROL", op_ROL, addr_ZPX, 2, 6 };
    lookup[0x2E] = (Instruction){ "ROL", op_ROL, addr_ABS, 3, 6 };
    lookup[0x3E] = (Instruction){ "ROL", op_ROL, addr_ABX, 3, 7 };

    lookup[0x6A] = (Instruction){ "ROR", op_ROR, addr_ACC, 1, 2 };
    lookup[0x66] = (Instruction){ "ROR", op_ROR, addr_ZPO, 2, 5 };
    lookup[0x76] = (Instruction){ "ROR", op_ROR, addr_ZPX, 2, 6 };
    lookup[0x6E] = (Instruction){ "ROR", op_ROR, addr_ABS, 3, 6 };
    lookup[0x7E] = (Instruction){ "ROR", op_ROR, addr_ABX, 3, 7 };
    // CONtROL
    lookup[0x4C] = (Instruction){ "JMP", op_JMP, addr_ABS, 3, 3 };
    lookup[0x6C] = (Instruction){ "JMP", op_JMP, addr_IND, 3, 5 };

    lookup[0x20] = (Instruction){ "JSR", op_JSR, addr_ABS, 3, 6 };
    lookup[0x60] = (Instruction){ "RTS", op_RTS, addr_IMP, 1, 6 };
    lookup[0x40] = (Instruction){ "RTI", op_RTI, addr_IMP, 1, 6 };
    lookup[0x00] = (Instruction){ "BRK", op_BRK, addr_IMP, 1, 7 };

    lookup[0xE6] = (Instruction){ "INC", op_INC, addr_ZPO, 2, 5 };
    lookup[0xF6] = (Instruction){ "INC", op_INC, addr_ZPX, 2, 6 };
    lookup[0xEE] = (Instruction){ "INC", op_INC, addr_ABS, 3, 6 };
    lookup[0xFE] = (Instruction){ "INC", op_INC, addr_ABX, 3, 7 };
    lookup[0xE8] = (Instruction){ "INX", op_INX, addr_IMP, 1, 2 };
    lookup[0xC8] = (Instruction){ "INY", op_INY, addr_IMP, 1, 2 };

    lookup[0xC6] = (Instruction){ "DEC", op_DEC, addr_ZPO, 2, 5 };
    lookup[0xD6] = (Instruction){ "DEC", op_DEC, addr_ZPX, 2, 6 };
    lookup[0xCE] = (Instruction){ "DEC", op_DEC, addr_ABS, 3, 6 };
    lookup[0xDE] = (Instruction){ "DEC", op_DEC, addr_ABX, 3, 7 };
    lookup[0xCA] = (Instruction){ "DEX", op_DEX, addr_IMP, 1, 2 };
    lookup[0x88] = (Instruction){ "DEY", op_DEY, addr_IMP, 1, 2 };

    lookup[0x18] = (Instruction){ "CLC", op_CLC, addr_IMP, 1, 2 };
    lookup[0x38] = (Instruction){ "SEC", op_SEC, addr_IMP, 1, 2 };
    lookup[0x58] = (Instruction){ "CLI", op_CLI, addr_IMP, 1, 2 };
    lookup[0x78] = (Instruction){ "SEI", op_SEI, addr_IMP, 1, 2 };
    lookup[0xB8] = (Instruction){ "CLV", op_CLV, addr_IMP, 1, 2 };
    lookup[0xD8] = (Instruction){ "CLD", op_CLD, addr_IMP, 1, 2 };
    lookup[0xF8] = (Instruction){ "SED", op_SED, addr_IMP, 1, 2 };

    // BIT (official NMOS 6502 variants only)
    lookup[0x24] = (Instruction){ "BIT", op_BIT, addr_ZPO, 2, 3 };
    lookup[0x2C] = (Instruction){ "BIT", op_BIT, addr_ABS, 3, 4 };

    // Unofficial NOPs (65C02 BIT opcodes → NOP on NMOS 6502, must skip operand bytes)
    lookup[0x89] = (Instruction){ "*NOP", op_NOOP, addr_IMM, 2, 2 };
    lookup[0x34] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };
    lookup[0x3C] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };

    lookup[0x1A] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };
    lookup[0x3A] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };
    lookup[0x5A] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };
    lookup[0x7A] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };
    lookup[0xDA] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };
    lookup[0xFA] = (Instruction){ "*NOP", op_NOOP, addr_IMP, 1, 2 };

    lookup[0x04] = (Instruction){ "*NOP", op_NOOP, addr_ZPO, 2, 3 };
    lookup[0x14] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };
    lookup[0x44] = (Instruction){ "*NOP", op_NOOP, addr_ZPO, 2, 3 };
    lookup[0x54] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };
    lookup[0x64] = (Instruction){ "*NOP", op_NOOP, addr_ZPO, 2, 3 };
    lookup[0x74] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };
    lookup[0x80] = (Instruction){ "*NOP", op_NOOP, addr_IMM, 2, 2 };
    lookup[0x82] = (Instruction){ "*NOP", op_NOOP, addr_IMM, 2, 2 };
    lookup[0xC2] = (Instruction){ "*NOP", op_NOOP, addr_IMM, 2, 2 };
    lookup[0xD4] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };
    lookup[0xE2] = (Instruction){ "*NOP", op_NOOP, addr_IMM, 2, 2 };
    lookup[0xF4] = (Instruction){ "*NOP", op_NOOP, addr_ZPX, 2, 4 };

    lookup[0x0C] = (Instruction){ "*NOP", op_NOOP, addr_ABS, 3, 4 };
    lookup[0x1C] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };
    lookup[0x5C] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };
    lookup[0x7C] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };
    lookup[0xDC] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };
    lookup[0xFC] = (Instruction){ "*NOP", op_NOOP, addr_ABX, 3, 4 };

    lookup[0xEB] = (Instruction){ "*SBC", op_SBC, addr_IMM, 2, 2 };







}


