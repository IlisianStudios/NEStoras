#include "cpu.h"
#include "bus.h"
#include "instruction.h"

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
}

instruction fetch(CPU *cpu) {
    const instruction inst = lookup[bus_read(cpu->pc)];
    cpu->pc++;
    return inst;
}

void cpu_step(CPU *cpu) {
    // next instruction using the pc point at mem
    instruction inst = fetch(cpu);

    uint8_t cycles = inst.cycles;

    uint8_t extra1 = inst.addrmode(cpu);
    uint8_t extra2 = inst.operate(cpu);

    cycles += extra1 & extra2;

    cpu->cycles += cycles;
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