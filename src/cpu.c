#include "cpu.h"
#include "bus.h"
#include "instruction.h"
#include "nestest_compare.h"
#include <stdio.h>

NestestLog nestest_log = {0};


void log_cpu_state(const CPU *cpu, Instruction inst, uint16_t pc) {
    // Print PC
    printf("%04X  ", pc);

    // Print instruction bytes
    for (uint8_t i = 0; i < inst.bytes; i++) {
        printf("%02X ", bus_read(pc + i));
    }

    // Pad to align mnemonic
    for (uint8_t i = inst.bytes; i < 3; i++) {
        printf("   ");
    }

    // Format mnemonic + operand
    char operand[32] = "";

    if (inst.bytes == 1) {
        snprintf(operand, sizeof(operand), "%s", inst.name);
    } else if (inst.bytes == 2) {
        uint8_t val = bus_read(pc + 1);
        if (inst.addrmode == &addr_IMM)
            snprintf(operand, sizeof(operand), "%s #$%02X", inst.name, val);
        else if (inst.addrmode == &addr_ZPO)
            snprintf(operand, sizeof(operand), "%s $%02X", inst.name, val);
        else if (inst.addrmode == &addr_ZPX)
            snprintf(operand, sizeof(operand), "%s $%02X,X", inst.name, val);
        else if (inst.addrmode == &addr_ZPY)
            snprintf(operand, sizeof(operand), "%s $%02X,Y", inst.name, val);
        else if (inst.addrmode == &addr_REL) {
            // Show the resolved branch target
            int8_t offset = (int8_t)val;
            uint16_t target = pc + 2 + offset;
            snprintf(operand, sizeof(operand), "%s $%04X", inst.name, target);
        }
        else if (inst.addrmode == &addr_IDX)
            snprintf(operand, sizeof(operand), "%s ($%02X,X)", inst.name, val);
        else if (inst.addrmode == &addr_IZY)
            snprintf(operand, sizeof(operand), "%s ($%02X),Y", inst.name, val);
        else
            snprintf(operand, sizeof(operand), "%s $%02X", inst.name, val);
    } else if (inst.bytes == 3) {
        uint16_t val = bus_read(pc + 1) | (bus_read(pc + 2) << 8);
        if (inst.addrmode == &addr_ABS)
            snprintf(operand, sizeof(operand), "%s $%04X", inst.name, val);
        else if (inst.addrmode == &addr_ABX)
            snprintf(operand, sizeof(operand), "%s $%04X,X", inst.name, val);
        else if (inst.addrmode == &addr_ABY)
            snprintf(operand, sizeof(operand), "%s $%04X,Y", inst.name, val);
        else if (inst.addrmode == &addr_IND)
            snprintf(operand, sizeof(operand), "%s ($%04X)", inst.name, val);
        else
            snprintf(operand, sizeof(operand), "%s $%04X", inst.name, val);
    }

    printf("%-31s", operand);

    // Replace with Ppu numbers. they get properly computed here and the whole nettestlog agrres with it
    uint32_t ppu_total = (uint32_t)(cpu->total_cycles * 3);
    uint16_t ppu_scanline = (ppu_total / 341) % 262;
    uint16_t ppu_dot = ppu_total % 341;

    // Print CPU registers
    printf("A:%02X X:%02X Y:%02X P:%02X SP:%02X PPU:%3u,%3u CYC:%llu\n",
           cpu->a,
           cpu->x,
           cpu->y,
           cpu->status,
           cpu->sp,
           ppu_scanline, ppu_dot,
           (unsigned long long)cpu->total_cycles);
}

void cpu_reset(CPU *cpu){
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD; // stack pointer starts at $01FD

    cpu->status = 0x24; // status register starts at 0

    // read reset vector
    const uint8_t low = bus_read(0xFFFC);
    const uint8_t high = bus_read(0xFFFD);
    cpu->pc = (high << 8) | low; // set program counter to reset vector
    cpu->cycles = 0;
    cpu->total_cycles = 7;
    cpu->testing_mode = false;
}


// non-maskable interrupt
void cpu_nmi(CPU *cpu){
    // pushes pc to stack
    stack_push(cpu, (cpu->pc >> 8) & 0xFF); // high byte
    stack_push(cpu, (cpu->pc & 0xFF)); // low byte
    // pushes flags to stack, forces U=1 (always 1 but forces it anyway), B=0 ONLY IN STACK
    // technically unneeded to force but 6502 does that so whatever
    stack_push(cpu, (cpu->status | FLAG_U) & ~FLAG_B);

    set_flag(cpu,FLAG_I,1); // disables further interrupts (since already in interrupt handling)

    // read from NMI vector
    uint8_t lo = bus_read(0xFFFA); // low byte
    uint8_t hi = bus_read(0xFFFB); // high byte
    // loads program counter with NMI handler 
    cpu->pc = (hi << 8) | lo;
}

// interrupt request
void cpu_irq(CPU *cpu){
    if (get_flag(cpu,FLAG_I)) return; // interrupts disabled

    // same as NMI
    stack_push(cpu, (cpu->pc >> 8) & 0xFF); // high byte
    stack_push(cpu, (cpu->pc & 0xFF)); // low byte
    stack_push(cpu, (cpu->status | FLAG_U) & ~FLAG_B);

    set_flag(cpu, FLAG_I, true);

    uint8_t lo = bus_read(0xFFFE);
    uint8_t hi = bus_read(0xFFFF);
    cpu->pc = (hi << 8) | lo;
}

void fetch(CPU *cpu) {
    const uint8_t opcode = bus_read(advance_pc(cpu));
    cpu->fetched = opcode;
}

void cpu_step(CPU *cpu) {
    cpu->cycles = 0;
    uint16_t pc_snapshot = cpu->pc;  // snapshot HERE
    fetch(cpu);
    Instruction inst = lookup[cpu->fetched];

    uint8_t extra1 = inst.addrmode(cpu);

    if (cpu->testing_mode)
        log_cpu_state(cpu, inst, pc_snapshot);  // pass snapshot

    #ifndef NDEBUG
    if (cpu->nestest_comp && !nestest_compare(&nestest_log, cpu, &inst, pc_snapshot))
        exit(1);
    #endif
    uint8_t extra2 = inst.operate(cpu);

    uint8_t cycles = inst.cycles + (extra1 & extra2);
    cpu->cycles += cycles;
    cpu->total_cycles += cpu->cycles ;
}

void set_flag(CPU *cpu, const uint8_t flag, const bool value) {
    if (value)
        cpu->status |= flag;
    else
        cpu->status &= ~flag;
}

bool get_flag(const CPU *cpu, const uint8_t flag) {
    return (cpu->status & flag) != 0;
}

void update_nz(CPU *cpu, uint8_t value) {
    set_flag(cpu, FLAG_Z, value ==0);
    set_flag(cpu, FLAG_N, value & FLAG_N);
}
