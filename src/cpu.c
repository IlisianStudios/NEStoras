#include "cpu.h"
#include "bus.h"
#include "instruction.h"
#include <stdio.h>
#include "cartridge.h"

void log_cpu_state(CPU *cpu, instruction inst) {
    // PC before instruction
    uint16_t pc_before = cpu->pc - inst.bytes;

    // Print PC
    printf("%04X  ", pc_before);

    // Print instruction bytes
    for (uint8_t i = 0; i < inst.bytes; i++) {
        printf("%02X ", bus_read(pc_before + i));
    }

    // Pad to 9 chars for alignment (nestest uses 3 bytes max)
    for (uint8_t i = inst.bytes; i < 3; i++) printf("   ");

    // Print mnemonic
    printf("%-28s", inst.name);

    // Print CPU registers after execution
    printf("A:%02X X:%02X Y:%02X P:%02X SP:%02X\n",
           cpu->a,
           cpu->x,
           cpu->y,
           cpu->status,
           cpu->sp);
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
    // next instruction using the pc point at mem
    fetch(cpu);
    instruction inst = lookup[cpu->fetched];

    uint8_t cycles = inst.cycles;

    uint8_t extra1 = inst.addrmode(cpu);
    uint8_t extra2 = inst.operate(cpu);

    cycles += extra1 & extra2;

    cpu->cycles += cycles;
    if (cpu->testing_mode)
        log_cpu_state(cpu, inst);
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
