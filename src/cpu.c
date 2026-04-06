#include "cpu.h"
#include "bus.h"
#include "instruction.h"
#include "nestest_compare.h"
#include "debug_window.h"
#include <stdio.h>
#include "apu.h"
#include "ringbuffer.h"

NestestLog nestest_log = {0};

void log_cpu_state(const CPU *cpu, Instruction inst, uint16_t pc) {
    // Format: "C000 4C F5C5 JMP $F5C5        A:00 X:00 Y:00 P:24 SP:FD"
    // Compact single-line format that fits in ~90 chars for the debug log.

    // Build hex bytes string (up to 3 bytes)
    char hex[10];
    int hlen = 0;
    for (uint8_t i = 0; i < inst.bytes; i++)
        hlen += snprintf(hex + hlen, sizeof(hex) - hlen, "%02X", bus_read(pc + i));

    // Build operand string
    char op[32] = "";
    if (inst.bytes == 2) {
        uint8_t val = bus_read(pc + 1);
        if (inst.addrmode == &addr_IMM)
            snprintf(op, sizeof(op), "#$%02X", val);
        else if (inst.addrmode == &addr_ZPO)
            snprintf(op, sizeof(op), "$%02X", val);
        else if (inst.addrmode == &addr_ZPX)
            snprintf(op, sizeof(op), "$%02X,X", val);
        else if (inst.addrmode == &addr_ZPY)
            snprintf(op, sizeof(op), "$%02X,Y", val);
        else if (inst.addrmode == &addr_REL)
            snprintf(op, sizeof(op), "$%04X", (uint16_t)(pc + 2 + (int8_t)val));
        else if (inst.addrmode == &addr_IDX)
            snprintf(op, sizeof(op), "($%02X,X)", val);
        else if (inst.addrmode == &addr_IZY)
            snprintf(op, sizeof(op), "($%02X),Y", val);
        else
            snprintf(op, sizeof(op), "$%02X", val);
    } else if (inst.bytes == 3) {
        uint16_t val = bus_read(pc + 1) | (bus_read(pc + 2) << 8);
        if (inst.addrmode == &addr_ABS)
            snprintf(op, sizeof(op), "$%04X", val);
        else if (inst.addrmode == &addr_ABX)
            snprintf(op, sizeof(op), "$%04X,X", val);
        else if (inst.addrmode == &addr_ABY)
            snprintf(op, sizeof(op), "$%04X,Y", val);
        else if (inst.addrmode == &addr_IND)
            snprintf(op, sizeof(op), "($%04X)", val);
        else
            snprintf(op, sizeof(op), "$%04X", val);
    }

    // Assemble mnemonic + operand
    char mnem[40];
    if (op[0])
        snprintf(mnem, sizeof(mnem), "%s %s", inst.name, op);
    else
        snprintf(mnem, sizeof(mnem), "%s", inst.name);

    debug_log("%04X %-6s %-14s A:%02X X:%02X Y:%02X S:%02X P:%02X",
              pc, hex, mnem,
              cpu->a, cpu->x, cpu->y, cpu->sp, cpu->status);
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

void audio_callback(void *userdata, Uint8 *stream, int len) {
    // SDL2 requires you to initialize the buffer.
    // Fill with 0 (silence) if no data is ready.
    int16_t *out = (int16_t *)stream;
    int num_samples = len / sizeof(int16_t);
    for (int i = 0; i < num_samples; i++) {
        out[i] = ring_buffer_pop();
    }
}

void run_cycles(CPU *cpu, uint64_t cycles) {
    uint32_t ran = 0;

    while (ran < cycles) {
        cpu_step(cpu);

        // cpu.cycles is how many cycles THIS instruction took (set inside cpu_step).
        // Tick the APU once per CPU cycle.
        for (uint8_t i = 0; i < cpu->cycles; i++) {
            apu_step(cpu);
        }

        ran += cpu->cycles;

        // In unbound mode we were called with max_cycles=1,
        // so this breaks after the first instruction.
        if (cycles == 1) break;

        // Safety: stop if somehow cycles is 0 (prevents infinite loop).
        if (cpu->cycles == 0) break;
    }
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
    if (cpu->nestest_comp && !nestest_compare(&nestest_log, cpu, &inst, pc_snapshot)) {
        cpu->nestest_comp = false;
        if (feof(nestest_log.file)) {
            printf("[NESTEST] ALL TESTS PASSED\n");
            cpu->nestest_passed = true;
        } else {
            printf("[NESTEST] FAILED — stopped comparing, emulator still running\n");
        }
        nestest_close(&nestest_log);
    }
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
