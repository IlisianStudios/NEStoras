# NEStoras — NES Emulator Design Document

> A hands-on guide to building a NES emulator in **C** with SDL2.
> Start with understanding, move to implementation, use the reference tables when you need the details.

> **Note:** NEStoras has progressed past the "from scratch" stage of this document. CPU, PPU, APU, NROM, and the debug window are all working and SMB plays through. The CPU implementation guide and hardware reference below are kept for understanding the design; the [Implementation Status](#implementation-status) section near the top and the marked-off [Development Roadmap](#development-roadmap) reflect what's actually built. The cycle-accurate PPU design lives in its own doc: [PPU_SPEC.md](PPU_SPEC.md).

---

## How to Read This Document

This document is split into two halves:

**Part 1 — Understanding & Implementation (read top to bottom)**
- Starts with a big-picture overview of the NES hardware
- Then walks you step by step through implementing the CPU — the first and most important piece of any emulator
- Written for people who have *never* done emulation or low-level programming before

**Part 2 — Hardware Reference (look things up as needed)**
- Detailed register tables, memory maps, bit layouts, and timing info
- You don't need to memorize any of this — come back to it when your code needs a specific value
- Think of it as a data sheet you keep open in another tab

**Part 3 — Plan & Resources**
- Development roadmap, test ROMs, debugging tips, and curated links

---

## Table of Contents

### Part 1 — Understanding & Implementation
0. [Implementation Status](#implementation-status) — **what currently works**
1. [Project Overview](#project-overview)
2. [NES Architecture Overview](#nes-architecture-overview)
3. [Hardware Specs at a Glance](#hardware-specs-at-a-glance)
4. [CPU Implementation Guide](#cpu-implementation-guide) — **start here after the overview**

### Part 2 — Hardware Reference
5. [CPU Reference — Ricoh 2A03 (MOS 6502 Core)](#cpu-reference--ricoh-2a03-mos-6502-core)
6. [CPU Memory Map](#cpu-memory-map)
7. [PPU — Picture Processing Unit (2C02)](#ppu--picture-processing-unit-2c02)
8. [PPU Memory Map](#ppu-memory-map)
9. [APU — Audio Processing Unit](#apu--audio-processing-unit)
10. [iNES ROM Format](#ines-rom-format)
11. [Mappers](#mappers)
12. [Controllers](#controllers)

### Part 3 — Plan & Resources
13. [Development Roadmap](#development-roadmap)
14. [Test ROMs & Debugging](#test-roms--debugging)
15. [Reference Links](#reference-links)

---

# Part 1 — Understanding & Implementation

## Implementation Status

What's working today (as of the `ppu-pandie` branch, May 2026):

| Subsystem | Status | Notes |
|---|---|---|
| **CPU** (Ricoh 2A03 / 6502 core) | ✅ | Full official instruction set + common illegal opcodes; passes `nestest.nes`; NMI/IRQ have proper 7-cycle service cost |
| **Memory bus** | ✅ | RAM mirroring, PPU register dispatch, APU/IO dispatch, OAMDMA, cartridge passthrough |
| **iNES loader** | ✅ | Detects PRG/CHR size, mapper ID, mirroring, PAL flag |
| **Mapper 0 — NROM** | ✅ | NROM-128 + NROM-256 with both common CHR sizes |
| **Other mappers** | ❌ | Not yet |
| **APU** | ✅ | Pulse 1 & 2 (envelope, sweep, length), triangle, noise (LFSR), DMC (memory reader + output unit, IRQ); 4-step + 5-step frame counter; nesdev mixer formulas; high-pass / low-pass filter chain; SDL2 audio with ring-buffer back-pressure as throttle |
| **PPU** (Ricoh 2C02) | ✅ | Cycle-accurate (per-dot) inside a catch-up step model called from `run_cycles`; full background fetch pipeline with `v`/`t`/`x`/`w` Loopy scroll; sprite evaluation + fetch + pixel mux; sprite 0 hit with all five hardware conditions; sprite overflow flag; correct OAM-Y +1 delay; pre-render line including odd-frame dot skip; ARGB framebuffer uploaded once per vblank |
| **PAL support** | ✅ | 312-scanline frame, PAL APU frame counter periods, PAL DMC rate table, 1:3.2 CPU-PPU ratio handled in `run_cycles` |
| **Controller 1** | ✅ | Keyboard mapped to the standard A/B/Select/Start + D-pad serial protocol |
| **Controller 2** | ❌ | Not yet |
| **Debug window** | ✅ | SDL2_ttf overlay with CPU instruction log, PPU state (Loopy regs, ctrl/mask/status, NMI count, sp0_hit/overflow), visual 8×8 sprite-grid OAM viewer (sprite 0 highlighted, change-detection caching), both CHR pattern tables, 32-entry palette swatches, APU per-channel scrolling scopes (P1/P2/TRI/NOI/DMC + mix), and live channel/length/volume state |
| **Save states** | ❌ | Not yet |

The PPU's design (cycle-accurate stepping, catch-up integration model, NMI semantics, the wiring/migration that ripped out an earlier APU-fake-NMI placeholder) lives in [PPU_SPEC.md](../PPU_SPEC.md). When in doubt about PPU behaviour, that's the canonical doc; this design document predates it and is kept for understanding the *general* NES architecture.

---

## Project Overview

**NEStoras** is a NES (Nintendo Entertainment System) emulator written in **C** (C11) using **SDL2** for cross-platform rendering, audio, and input.

If you've never built an emulator before, here's the short version: an emulator is a program that pretends to be a piece of hardware. Your code reads the same game ROM that the original console would read, and does the same work the console's chips would do — processing instructions, drawing pixels, producing sound. The result is a program on your computer that runs NES games.

This is a big project, but it's very doable if you take it one piece at a time. The CPU comes first — and this document will walk you through it in detail.

### Goals

- Accurately emulate CPU, PPU, APU, and common mappers
- Run major commercial titles (Super Mario Bros, Donkey Kong, Mega Man, etc.)
- Clean, well-documented codebase suitable for learning
- Cross-platform via SDL2 (macOS, Linux, Windows)

### Tech Stack

| Component       | Choice              |
|-----------------|---------------------|
| Language        | C (C11)             |
| Graphics/Input  | SDL2                |
| Audio           | SDL2 Audio          |
| Debug overlay   | SDL2_ttf            |
| Build System    | CMake               |
| ROM Format      | iNES (.nes)         |

---

## NES Architecture Overview

Before looking at any code, you need a mental model of how the NES works. The NES has a few major chips, and understanding how they talk to each other is more important than memorizing register details.

**The key players:**
- **CPU (Ricoh 2A03)** — The brain. It runs the game's code, which is stored on the cartridge. It's based on the MOS 6502, a simple 8-bit processor from the 1970s.
- **PPU (Ricoh 2C02)** — The graphics chip. It draws tiles and sprites to the screen. The CPU tells it *what* to draw by writing to PPU registers — but the PPU does the actual pixel work on its own.
- **APU** — The audio hardware. It's actually *inside* the CPU chip, but it's logically separate. It generates sound from 5 channels.
- **Cartridge** — Not just storage! Cartridges contain the game ROM plus (often) extra hardware called a "mapper" that extends the console's capabilities.
- **2 KB RAM** — A tiny amount of working memory for the CPU.
- **Controllers** — Two button-pads read via a simple serial protocol.

```
┌─────────────────────────────────────────────────────────┐
│                     NES Console                         │
│                                                         │
│  ┌──────────┐    ┌──────────┐    ┌──────────────────┐   │
│  │          │    │          │    │                  │   │
│  │   CPU    │◄──►│   PPU    │◄──►│   Cartridge      │   │
│  │ (2A03)   │    │ (2C02)   │    │  ┌────────────┐ │   │
│  │          │    │          │    │  │ PRG ROM     │ │   │
│  │ - 6502   │    │ - Tiles  │    │  │ CHR ROM     │ │   │
│  │ - APU    │    │ - Sprites│    │  │ Mapper HW   │ │   │
│  │ - I/O    │    │ - BG     │    │  │ (optional   │ │   │
│  │          │    │ - Scroll │    │  │  RAM/battery)│ │   │
│  └────┬─────┘    └──────────┘    │  └────────────┘ │   │
│       │                          └──────────────────┘   │
│       │                                                 │
│  ┌────┴─────┐    ┌──────────┐                           │
│  │  2 KB    │    │Controller│                           │
│  │  RAM     │    │  Port(s) │                           │
│  └──────────┘    └──────────┘                           │
└─────────────────────────────────────────────────────────┘
```

The CPU and PPU run **simultaneously** on separate buses. This is important — they're like two workers sharing a factory. They each have their own workspace:

- The **CPU bus** accesses: internal RAM, PPU registers, APU/IO registers, and cartridge PRG ROM/RAM.
- The **PPU bus** accesses: pattern tables (CHR ROM/RAM), nametables (VRAM), and palettes — all managed by the cartridge mapper.

The way the CPU "talks" to the PPU is by writing to special memory addresses ($2000–$2007). The PPU doesn't execute game code — it just follows the instructions that the CPU puts in those registers.

They're synchronized by a master clock. For NTSC:
- **CPU** = master clock ÷ 12
- **PPU** = master clock ÷ 4
- This means **1 CPU cycle = 3 PPU cycles**

In your emulator, the simplest approach is: run one CPU instruction, then run 3× that many PPU cycles. This keeps them in sync.

---

## Hardware Specs at a Glance

This table is your quick-reference card. Don't try to memorize it — just know it's here when you need a number.

| Component | Specification |
|-----------|---------------|
| **CPU** | Ricoh 2A03 (custom MOS 6502, no BCD mode) |
| **CPU Clock** | 1.789773 MHz (NTSC) / 1.662607 MHz (PAL) |
| **PPU** | Ricoh 2C02 |
| **PPU Clock** | 5.369318 MHz (NTSC) — 3× CPU clock |
| **Resolution** | 256 × 240 pixels |
| **Colors** | 64-color palette, 25 colors on-screen simultaneously |
| **Sprites** | 64 total, 8 per scanline, 8×8 or 8×16 pixels |
| **CPU RAM** | 2 KB ($0000–$07FF, mirrored to $1FFF) |
| **VRAM** | 2 KB (nametables) |
| **Palette RAM** | 32 bytes (background + sprite palettes) |
| **APU Channels** | 2 pulse, 1 triangle, 1 noise, 1 DMC (delta modulation) |
| **Controller** | 8 buttons: A, B, Select, Start, Up, Down, Left, Right |
| **Cartridge** | PRG ROM (code), CHR ROM (graphics), optional mapper hardware |

---

| **Cartridge** | PRG ROM (code), CHR ROM (graphics), optional mapper hardware |

---

## CPU Implementation Guide

This is the most important section of this document. If you've never done emulation or low-level programming, start here. By the end of this section, you'll have a clear picture of what you need to build and how to build it.

### What Does "Emulating a CPU" Actually Mean?

A real NES CPU is a physical chip with transistors. It reads bytes from memory, interprets them as instructions, and changes its internal state (registers, flags, memory). That's it — it's a loop:

1. **Fetch** — Read the byte at the current program counter (PC)
2. **Decode** — Figure out which instruction that byte represents
3. **Execute** — Do what the instruction says (math, memory access, jump somewhere, etc.)
4. **Repeat**

Your emulator does the same thing in software. You replace the physical chip with a `struct` holding the register values, and you replace the fetch-decode-execute loop with a function that reads bytes from an array and uses a `switch` statement.

That's the entire concept. Everything else is details.

### Step 1 — Define the CPU State

The 6502 CPU has very little state. Here's everything you need to track:

```c
typedef struct {
    uint8_t  a;      // Accumulator
    uint8_t  x;      // Index register X
    uint8_t  y;      // Index register Y
    uint8_t  sp;     // Stack pointer (points into $0100-$01FF)
    uint16_t pc;     // Program counter (address of next instruction)
    uint8_t  status; // Status register (flags: NV-BDIZC)
} CPU;
```

That's 7 bytes of state. The entire CPU fits in a small struct. When you hear "6502 emulation," this is what you're simulating — how these few values change over time as instructions are executed.

Initialize it like the real hardware does on power-up:

```c
void cpu_reset(CPU *cpu) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD;          // Stack pointer starts at $FD
    cpu->status = 0x24;       // IRQ disabled, unused bit set
    // Read the reset vector at $FFFC-$FFFD to get the starting address
    uint8_t lo = bus_read(0xFFFC);
    uint8_t hi = bus_read(0xFFFD);
    cpu->pc = (hi << 8) | lo;
}
```

Notice `bus_read()` — that's how the CPU accesses memory. More on that next.

### Step 2 — Build the Memory Bus

The CPU doesn't access memory directly — it goes through an **address bus**. When the CPU reads address `$0200`, the bus decides what hardware responds. Think of the bus as a switchboard:

```c
uint8_t bus_read(uint16_t address) {
    if (address < 0x2000) {
        // Internal RAM (2 KB, mirrored every $0800)
        return ram[address & 0x07FF];
    }
    else if (address < 0x4000) {
        // PPU registers (8 registers, mirrored every 8 bytes)
        return ppu_register_read(address & 0x2007);
    }
    else if (address < 0x4020) {
        // APU and I/O registers
        return apu_io_read(address);
    }
    else {
        // Cartridge space ($4020-$FFFF) — PRG ROM, SRAM, etc.
        return cartridge_read(address);
    }
}

void bus_write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        ram[address & 0x07FF] = value;
    }
    else if (address < 0x4000) {
        ppu_register_write(address & 0x2007, value);
    }
    else if (address < 0x4020) {
        apu_io_write(address, value);
    }
    else {
        cartridge_write(address, value);
    }
}
```

**Why mirroring?** The NES only has 2 KB of RAM, but the address space up to $1FFF is 8 KB. The hardware just ignores the upper bits — so $0000, $0800, $1000, and $1800 all point to the same physical byte. The `& 0x07FF` mask does this.

At first, your `ppu_register_read` and `apu_io_read` can just return 0. The CPU doesn't care what the PPU does — it just reads and writes addresses. You'll fill those in later.

### Step 3 — The Fetch-Decode-Execute Loop

This is the heart of your emulator. Every call to `cpu_step()` executes one instruction:

```c
int cpu_step(CPU *cpu) {
    // FETCH: read the opcode at the current PC
    uint8_t opcode = bus_read(cpu->pc);
    cpu->pc++;

    // DECODE + EXECUTE: do what the opcode says
    switch (opcode) {
        case 0xA9: {  // LDA Immediate
            uint8_t value = bus_read(cpu->pc);
            cpu->pc++;
            cpu->a = value;
            // Update flags
            set_flag_z(cpu, cpu->a);
            set_flag_n(cpu, cpu->a);
            return 2;  // This instruction takes 2 CPU cycles
        }

        case 0x00: {  // BRK
            // ... handle BRK
            return 7;
        }

        // ... 254 more cases (one per opcode byte)

        default:
            // Unknown opcode — log it and decide what to do
            printf("Unknown opcode: %02X at %04X\n", opcode, cpu->pc - 1);
            return 2;
    }
}
```

The return value is the number of CPU cycles that instruction took. You'll need this later to keep the CPU and PPU in sync.

**The big switch statement:** There are 256 possible opcode bytes (0x00–0xFF). About 150 of them are official instructions, and the rest are "illegal" opcodes (some games use these, but you can add them later). Yes, the switch statement is large — that's normal and expected. This is the standard approach.

### Step 4 — Understand Addressing Modes

Here's where beginners often get confused. The 6502 has 13 "addressing modes" — but they're really just 13 different ways to answer one question: **"where is the data?"**

Think of every instruction as having two parts:
- **What to do** (add, subtract, load, store, compare, jump...)
- **Where to find the operand** (the data to work with)

For example, `LDA` means "load a value into the A register." But *which* value? That depends on the addressing mode:

```
LDA #$42       ; Immediate — the value IS $42 (literal, right in the code)
LDA $80        ; Zero Page — read the value from memory address $0080
LDA $80,X      ; Zero Page,X — read from address ($0080 + X), wrapping within page zero
LDA $1234      ; Absolute — read from memory address $1234
LDA $1234,X    ; Absolute,X — read from address ($1234 + X)
LDA ($80,X)    ; (Indirect,X) — use ($80+X) as a pointer to find the real address
LDA ($80),Y    ; (Indirect),Y — use $80 as a pointer, then add Y to that address
```

In code, you can handle this by writing helper functions that resolve the address first, then the instruction just works with the result:

```c
// Returns the address that the operand refers to
uint16_t get_operand_address(CPU *cpu, AddressingMode mode) {
    switch (mode) {
        case IMMEDIATE:
            return cpu->pc++;  // The operand IS the next byte

        case ZERO_PAGE: {
            uint8_t addr = bus_read(cpu->pc++);
            return addr;       // Address in $00-$FF
        }

        case ZERO_PAGE_X: {
            uint8_t addr = bus_read(cpu->pc++);
            return (addr + cpu->x) & 0xFF;  // Wraps within zero page
        }

        case ABSOLUTE: {
            uint8_t lo = bus_read(cpu->pc++);
            uint8_t hi = bus_read(cpu->pc++);
            return (hi << 8) | lo;  // Full 16-bit address
        }

        case ABSOLUTE_X: {
            uint8_t lo = bus_read(cpu->pc++);
            uint8_t hi = bus_read(cpu->pc++);
            return ((hi << 8) | lo) + cpu->x;
        }

        case ABSOLUTE_Y: {
            uint8_t lo = bus_read(cpu->pc++);
            uint8_t hi = bus_read(cpu->pc++);
            return ((hi << 8) | lo) + cpu->y;
        }

        case INDIRECT_X: {
            uint8_t base = bus_read(cpu->pc++);
            uint8_t ptr = (base + cpu->x) & 0xFF;
            uint8_t lo = bus_read(ptr);
            uint8_t hi = bus_read((ptr + 1) & 0xFF);
            return (hi << 8) | lo;
        }

        case INDIRECT_Y: {
            uint8_t ptr = bus_read(cpu->pc++);
            uint8_t lo = bus_read(ptr);
            uint8_t hi = bus_read((ptr + 1) & 0xFF);
            return ((hi << 8) | lo) + cpu->y;
        }

        // ... RELATIVE, INDIRECT, ACCUMULATOR, IMPLIED
    }
}
```

Now your instruction handlers become clean:

```c
case 0xA5: {  // LDA Zero Page
    uint16_t addr = get_operand_address(cpu, ZERO_PAGE);
    cpu->a = bus_read(addr);
    set_flag_z(cpu, cpu->a);
    set_flag_n(cpu, cpu->a);
    return 3;
}

case 0xA9: {  // LDA Immediate
    uint16_t addr = get_operand_address(cpu, IMMEDIATE);
    cpu->a = bus_read(addr);
    set_flag_z(cpu, cpu->a);
    set_flag_n(cpu, cpu->a);
    return 2;
}
```

Notice that `LDA Zero Page` and `LDA Immediate` do the same thing — the only difference is how they get the address. This is the power of separating addressing modes from instructions.

### Step 5 — Status Flags

The 6502 status register is 8 bits, where each bit is a flag. Most instructions update some of these flags based on the result. Here are helper functions:

```c
// Flag bit positions
#define FLAG_C 0x01  // Carry
#define FLAG_Z 0x02  // Zero
#define FLAG_I 0x04  // Interrupt disable
#define FLAG_D 0x08  // Decimal (ignored on NES, but the bit exists)
#define FLAG_B 0x10  // Break
#define FLAG_U 0x20  // Unused (always 1)
#define FLAG_V 0x40  // Overflow
#define FLAG_N 0x80  // Negative

void set_flag(CPU *cpu, uint8_t flag, bool value) {
    if (value)
        cpu->status |= flag;
    else
        cpu->status &= ~flag;
}

bool get_flag(CPU *cpu, uint8_t flag) {
    return (cpu->status & flag) != 0;
}

// Most instructions update N and Z, so make a helper:
void update_nz(CPU *cpu, uint8_t value) {
    set_flag(cpu, FLAG_Z, value == 0);
    set_flag(cpu, FLAG_N, value & 0x80);
}
```

**When to update which flags:**
- **N and Z** — Almost every arithmetic/logic/load instruction updates these. N is set if bit 7 of the result is 1 (meaning the value is "negative" in signed math). Z is set if the result is zero.
- **C (Carry)** — Updated by ADC, SBC, CMP, ASL, LSR, ROL, ROR. For addition, carry means the result exceeded 255. For comparison, carry means A >= M.
- **V (Overflow)** — Only updated by ADC, SBC, and BIT. This is the trickiest flag — it detects *signed* overflow. See the ADC section below.

### Step 6 — Implement Instructions (Start Small)

Don't try to implement all 150+ opcodes at once. Start with these groups in this order:

**1. Load/Store — LDA, LDX, LDY, STA, STX, STY**

These are the simplest. `LDA` loads a value into A and updates N,Z. `STA` stores A into memory and updates nothing. Once these work, the CPU can move data around.

**2. Transfer — TAX, TAY, TXA, TYA, TSX, TXS**

Copy one register to another. `TAX` copies A to X and updates N,Z. `TXS` copies X to SP and updates *no* flags (this is the only one that doesn't).

**3. Stack — PHA, PLA, PHP, PLP**

Push/pull values to/from the stack. The stack lives at $0100–$01FF, and SP points to the *next free slot*:

```c
void stack_push(CPU *cpu, uint8_t value) {
    bus_write(0x0100 + cpu->sp, value);
    cpu->sp--;
}

uint8_t stack_pull(CPU *cpu) {
    cpu->sp++;
    return bus_read(0x0100 + cpu->sp);
}
```

Watch out: the stack grows *downward*. Push decrements SP, pull increments SP.

**4. Arithmetic — ADC, SBC**

`ADC` (add with carry) is the only addition instruction. Here's the full implementation:

```c
void op_adc(CPU *cpu, uint8_t value) {
    uint16_t sum = cpu->a + value + (get_flag(cpu, FLAG_C) ? 1 : 0);

    set_flag(cpu, FLAG_C, sum > 0xFF);
    set_flag(cpu, FLAG_V, (~(cpu->a ^ value) & (cpu->a ^ sum)) & 0x80);
    cpu->a = sum & 0xFF;
    update_nz(cpu, cpu->a);
}
```

The overflow flag formula looks cryptic. Here's what it means: overflow happens when adding two positive numbers gives a negative result, or adding two negative numbers gives a positive result. The formula checks if the sign of A and the value matched *before* the add, but the result's sign is different.

`SBC` (subtract with carry) is implemented as: `A = A - value - (1 - C)`. On the 6502, `SBC` is equivalent to `ADC` with the value inverted:

```c
void op_sbc(CPU *cpu, uint8_t value) {
    op_adc(cpu, ~value);  // SBC is just ADC with the value bitwise-inverted
}
```

This works because of how two's complement arithmetic and the carry flag interact. If you're not sure why, just trust it — it will pass all the test ROMs.

**5. Logic — AND, ORA, EOR**

These are straightforward bitwise operations:

```c
cpu->a = cpu->a & value;  // AND
cpu->a = cpu->a | value;  // ORA
cpu->a = cpu->a ^ value;  // EOR
update_nz(cpu, cpu->a);
```

**6. Compare — CMP, CPX, CPY**

Comparison works like subtraction but *doesn't store the result* — it only sets flags:

```c
void op_compare(CPU *cpu, uint8_t register_value, uint8_t memory_value) {
    uint8_t result = register_value - memory_value;
    set_flag(cpu, FLAG_C, register_value >= memory_value);
    update_nz(cpu, result);
}
```

**7. Branches — BCC, BCS, BEQ, BNE, BMI, BPL, BVC, BVS**

All 8 branch instructions work the same way: check a condition, and if true, add a *signed* offset to PC:

```c
void op_branch(CPU *cpu, bool condition) {
    int8_t offset = (int8_t)bus_read(cpu->pc);  // Signed 8-bit offset
    cpu->pc++;
    if (condition) {
        cpu->pc += offset;
    }
}

// In the switch:
case 0xD0:  // BNE — branch if Z flag is clear
    op_branch(cpu, !get_flag(cpu, FLAG_Z));
    ...
```

**8. Shifts and Rotates — ASL, LSR, ROL, ROR**

These shift bits left or right, moving bits through the carry flag:

```c
// ASL: shift left, old bit 7 goes to carry
void op_asl(CPU *cpu, uint16_t addr, bool accumulator) {
    uint8_t value = accumulator ? cpu->a : bus_read(addr);
    set_flag(cpu, FLAG_C, value & 0x80);
    value <<= 1;
    if (accumulator) cpu->a = value;
    else bus_write(addr, value);
    update_nz(cpu, value);
}
```

**9. Jumps and Subroutines — JMP, JSR, RTS, RTI, BRK**

`JSR` (jump to subroutine) pushes the return address minus one to the stack, then jumps. `RTS` pulls it back and adds one:

```c
case 0x20: {  // JSR Absolute
    uint8_t lo = bus_read(cpu->pc++);
    uint8_t hi = bus_read(cpu->pc++);
    // Push PC-1 (the address of the last byte of this instruction)
    stack_push(cpu, (cpu->pc - 1) >> 8);
    stack_push(cpu, (cpu->pc - 1) & 0xFF);
    cpu->pc = (hi << 8) | lo;
    return 6;
}

case 0x60: {  // RTS
    uint8_t lo = stack_pull(cpu);
    uint8_t hi = stack_pull(cpu);
    cpu->pc = ((hi << 8) | lo) + 1;
    return 6;
}
```

**10. Inc/Dec, Flag ops, NOP, BIT** — These are all simple once you have the patterns above.

### Step 7 — Use a Lookup Table (Optional but Recommended)

Once you understand the switch approach, consider organizing your opcodes into a lookup table. This makes the code shorter and less error-prone:

```c
typedef struct {
    char     *name;       // Mnemonic (for logging)
    void     (*execute)(CPU *cpu, uint16_t addr);
    AddrMode  mode;       // Addressing mode
    uint8_t   cycles;     // Base cycle count
    uint8_t   bytes;      // Instruction length
} Instruction;

Instruction instruction_table[256];

// Fill it in during initialization:
instruction_table[0xA9] = (Instruction){"LDA", op_lda, IMMEDIATE, 2, 2};
instruction_table[0xA5] = (Instruction){"LDA", op_lda, ZERO_PAGE, 3, 2};
instruction_table[0xB5] = (Instruction){"LDA", op_lda, ZERO_PAGE_X, 4, 2};
instruction_table[0xAD] = (Instruction){"LDA", op_lda, ABSOLUTE, 4, 3};
// ... and so on for all opcodes
```

Then your step function becomes:

```c
int cpu_step(CPU *cpu) {
    uint8_t opcode = bus_read(cpu->pc++);
    Instruction *instr = &instruction_table[opcode];
    uint16_t addr = get_operand_address(cpu, instr->mode);
    instr->execute(cpu, addr);
    return instr->cycles;
}
```

Both approaches (big switch vs lookup table) work fine. The lookup table is cleaner; the switch is easier to debug because each opcode is self-contained. Pick whichever feels right. Many successful emulators use each style.

### Step 8 — Interrupts

The 6502 has three types of interrupts: RESET, NMI, and IRQ. You've already seen RESET in `cpu_reset()`. The other two work almost identically:

```c
void cpu_nmi(CPU *cpu) {
    // Push PC and status to stack
    stack_push(cpu, cpu->pc >> 8);
    stack_push(cpu, cpu->pc & 0xFF);
    stack_push(cpu, (cpu->status | FLAG_U) & ~FLAG_B);
    set_flag(cpu, FLAG_I, true);  // Disable further IRQs
    // Load PC from NMI vector
    uint8_t lo = bus_read(0xFFFA);
    uint8_t hi = bus_read(0xFFFB);
    cpu->pc = (hi << 8) | lo;
}

void cpu_irq(CPU *cpu) {
    if (get_flag(cpu, FLAG_I)) return;  // IRQ is masked
    stack_push(cpu, cpu->pc >> 8);
    stack_push(cpu, cpu->pc & 0xFF);
    stack_push(cpu, (cpu->status | FLAG_U) & ~FLAG_B);
    set_flag(cpu, FLAG_I, true);
    uint8_t lo = bus_read(0xFFFE);
    uint8_t hi = bus_read(0xFFFF);
    cpu->pc = (hi << 8) | lo;
}
```

**NMI** (Non-Maskable Interrupt) is fired by the PPU once per frame when VBlank starts. This is how games know it's time to update graphics.

**IRQ** (Interrupt Request) is used by mappers and the APU. It can be ignored if the I flag is set.

**BRK** works like an IRQ but sets the B flag in the pushed status byte, and pushes PC+2 (skipping a padding byte).

### Step 9 — Load a ROM and Wire It Up

Once your CPU can execute instructions, you need to feed it a NES ROM. The iNES format is simple (full details in the reference section below):

```c
typedef struct {
    uint8_t *prg_rom;      // Program ROM (code)
    uint8_t *chr_rom;      // Character ROM (graphics)
    uint32_t prg_size;     // Size in bytes
    uint32_t chr_size;     // Size in bytes
    uint8_t  mapper;       // Mapper number
    uint8_t  mirroring;    // 0=horizontal, 1=vertical
} Cartridge;

bool cartridge_load(Cartridge *cart, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t header[16];
    fread(header, 1, 16, f);

    // Verify "NES\x1A" magic
    if (header[0] != 'N' || header[1] != 'E' ||
        header[2] != 'S' || header[3] != 0x1A) {
        fclose(f);
        return false;
    }

    cart->prg_size = header[4] * 16384;  // PRG ROM in 16KB units
    cart->chr_size = header[5] * 8192;   // CHR ROM in 8KB units
    cart->mapper = (header[7] & 0xF0) | (header[6] >> 4);
    cart->mirroring = header[6] & 0x01;

    // Skip trainer if present
    if (header[6] & 0x04) fseek(f, 512, SEEK_CUR);

    cart->prg_rom = malloc(cart->prg_size);
    fread(cart->prg_rom, 1, cart->prg_size, f);

    if (cart->chr_size > 0) {
        cart->chr_rom = malloc(cart->chr_size);
        fread(cart->chr_rom, 1, cart->chr_size, f);
    }

    fclose(f);
    return true;
}
```

For Mapper 0 (the simplest), reading from the cartridge is straightforward:

```c
uint8_t cartridge_read(uint16_t address) {
    if (address >= 0x8000) {
        // For 16KB PRG: mirror ($8000-$BFFF = $C000-$FFFF)
        // For 32KB PRG: map directly
        uint16_t offset = address - 0x8000;
        if (cart.prg_size == 16384) offset &= 0x3FFF;
        return cart.prg_rom[offset];
    }
    return 0;
}
```

### Step 10 — Testing with nestest.nes

This is your most important milestone. **nestest.nes** is a test ROM that exercises every official CPU opcode. It can run in "automation mode" — you start execution at address $C000 (instead of reading the reset vector), and it runs through all the tests without needing a PPU.

**How to test:**
1. Load nestest.nes
2. Set `cpu->pc = 0xC000` (override the reset vector)
3. Run instructions in a loop, logging each one
4. Compare your log line by line against the reference nestest.log

**Your log format should look like this:**

```
C000  4C F5 C5  JMP $C5F5                       A:00 X:00 Y:00 P:24 SP:FD CYC:7
C5F5  A2 00     LDX #$00                        A:00 X:00 Y:00 P:24 SP:FD CYC:10
C5F7  86 00     STX $00 = 00                    A:00 X:00 Y:00 P:26 SP:FD CYC:12
```

Each line shows: PC, raw bytes, disassembly, register state *before* execution, and cumulative cycle count. When your log differs from the reference, you've found a bug — fix it, re-run, repeat.

> **Where to get nestest.nes and the reference log:** Search for "nestest.nes" — it's freely available in the NES homebrew community. The reference log is usually distributed alongside it.

**Common first-run issues:**
- Flags wrong after ADC/SBC (check carry and overflow logic)
- Branch offsets treated as unsigned instead of signed
- Forgetting that `BRK` pushes PC+2, not PC+1
- Stack push/pull direction reversed (push = write then decrement, pull = increment then read)
- Indirect JMP page boundary bug not implemented

Once nestest passes all official opcodes, your CPU is solid. Everything else builds on this foundation.

### Putting It All Together — The Main Loop

Here's how these pieces connect into a running emulator:

```c
int main() {
    CPU cpu;
    Cartridge cart;

    cartridge_load(&cart, "game.nes");

    // Initialize 2KB RAM to zero
    memset(ram, 0, sizeof(ram));

    // Reset CPU (reads reset vector from ROM)
    cpu_reset(&cpu);

    // Main emulation loop
    while (running) {
        int cycles = cpu_step(&cpu);

        // For every CPU cycle, run 3 PPU cycles
        for (int i = 0; i < cycles * 3; i++) {
            ppu_step();  // Stub this out at first
        }

        // Check if PPU generated an NMI
        if (ppu_nmi_pending) {
            cpu_nmi(&cpu);
            ppu_nmi_pending = false;
        }
    }
}
```

At first, `ppu_step()` can be an empty function. You're just running CPU instructions. Once the CPU passes its tests, you add the PPU, then controllers, then sound. One layer at a time.

### Summary — Your First Weekend

Here's a realistic sequence for getting a working CPU:

1. **Hour 1–2:** Set up the project (CMake + SDL2 window), define the CPU struct and memory arrays
2. **Hour 3–4:** Implement `bus_read`/`bus_write`, ROM loading, and `cpu_reset`
3. **Hour 5–8:** Implement the first 20 opcodes (LDA, STA, LDX, LDY, STX, STY, JMP, JSR, RTS, NOP, SEC, CLC, BNE, BEQ, INX, DEX, TAX, TXA, CMP, ADC) — enough to start running nestest
4. **Hour 9–12:** Add CPU logging, compare against nestest.log, fix bugs, add more opcodes as nestest needs them
5. **Hour 13+:** Keep going until nestest passes completely

You'll spend most of your time in step 4 — and that's fine. Debugging against nestest is the most efficient way to get your CPU right. Every time the log diverges, you know exactly which instruction is wrong.

---

# Part 2 — Hardware Reference

Everything below is reference material. Come back here when your code needs a specific register bit, memory address, or timing value.

---

## CPU Reference — Ricoh 2A03 (MOS 6502 Core)

The NES CPU is a Ricoh 2A03, which contains a MOS 6502 core (with **BCD mode disabled**) plus the APU and I/O controller.

### Registers

| Register | Size    | Description |
|----------|---------|-------------|
| **A**    | 8-bit   | Accumulator — main register for arithmetic/logic |
| **X**    | 8-bit   | Index register X — used for indexing and loop counters |
| **Y**    | 8-bit   | Index register Y — used for indexing and loop counters |
| **SP**   | 8-bit   | Stack pointer — points into page $01 ($0100–$01FF) |
| **PC**   | 16-bit  | Program counter — address of next instruction |
| **P**    | 8-bit   | Status register (flags) |

### Status Register (P) — Flags

```
  Bit:  7  6  5  4  3  2  1  0
        N  V  -  B  D  I  Z  C
```

| Bit | Flag | Name | Description |
|-----|------|------|-------------|
| 7   | **N** | Negative   | Set if result bit 7 is set (sign bit) |
| 6   | **V** | Overflow   | Set on signed arithmetic overflow |
| 5   | **-** | (unused)   | Always 1 when pushed to stack |
| 4   | **B** | Break      | Set when BRK pushes status; cleared on IRQ/NMI |
| 3   | **D** | Decimal    | BCD mode flag (exists but **non-functional** on 2A03) |
| 2   | **I** | Interrupt  | When set, IRQ is disabled (NMI still fires) |
| 1   | **Z** | Zero       | Set if result is zero |
| 0   | **C** | Carry      | Set on unsigned overflow / borrow |

### Addressing Modes (13 modes)

| Mode | Syntax | Example | Bytes | Description |
|------|--------|---------|-------|-------------|
| Implied | `OPC` | `CLC` | 1 | Operand implicit in instruction |
| Accumulator | `OPC A` | `ROL A` | 1 | Operates on accumulator |
| Immediate | `OPC #val` | `LDA #$44` | 2 | Literal 8-bit value |
| Zero Page | `OPC $LL` | `LDA $80` | 2 | Address in $0000–$00FF |
| Zero Page,X | `OPC $LL,X` | `LDA $80,X` | 2 | Zero-page + X (wraps in zero page) |
| Zero Page,Y | `OPC $LL,Y` | `LDX $60,Y` | 2 | Zero-page + Y (wraps in zero page) |
| Absolute | `OPC $HHLL` | `JMP $4000` | 3 | Full 16-bit address |
| Absolute,X | `OPC $HHLL,X` | `LDA $3120,X` | 3 | Absolute + X (may cross pages, +1 cycle) |
| Absolute,Y | `OPC $HHLL,Y` | `LDA $8240,Y` | 3 | Absolute + Y (may cross pages, +1 cycle) |
| Indirect | `OPC ($HHLL)` | `JMP ($FF82)` | 3 | 16-bit pointer lookup (JMP only) |
| (Indirect,X) | `OPC ($LL,X)` | `LDA ($70,X)` | 2 | Pre-indexed indirect via zero page |
| (Indirect),Y | `OPC ($LL),Y` | `LDA ($70),Y` | 2 | Post-indexed indirect via zero page |
| Relative | `OPC $BB` | `BEQ $05` | 2 | Signed 8-bit offset for branches |

> **Note on Indirect JMP bug (NMOS 6502):** If the indirect vector falls on a page boundary (low byte = $FF), the high byte is fetched from $xx00 instead of $(xx+1)00. Example: `JMP ($10FF)` reads low byte from $10FF and high byte from $1000 (not $1100). This hardware bug must be emulated.

### Instruction Set Summary (56 Official Instructions)

#### Arithmetic & Logic
| Mnemonic | Description | Flags Affected |
|----------|-------------|----------------|
| ADC | Add with carry | N, V, Z, C |
| SBC | Subtract with carry | N, V, Z, C |
| AND | Bitwise AND with A | N, Z |
| ORA | Bitwise OR with A | N, Z |
| EOR | Bitwise XOR with A | N, Z |
| ASL | Arithmetic shift left | N, Z, C |
| LSR | Logical shift right | N(=0), Z, C |
| ROL | Rotate left through carry | N, Z, C |
| ROR | Rotate right through carry | N, Z, C |
| BIT | Bit test (A AND M) | N=M7, V=M6, Z |

#### Load / Store
| Mnemonic | Description | Flags Affected |
|----------|-------------|----------------|
| LDA | Load accumulator | N, Z |
| LDX | Load X register | N, Z |
| LDY | Load Y register | N, Z |
| STA | Store accumulator | — |
| STX | Store X register | — |
| STY | Store Y register | — |

#### Compare
| Mnemonic | Description | Flags Affected |
|----------|-------------|----------------|
| CMP | Compare A with memory | N, Z, C |
| CPX | Compare X with memory | N, Z, C |
| CPY | Compare Y with memory | N, Z, C |

#### Increment / Decrement
| Mnemonic | Description | Flags Affected |
|----------|-------------|----------------|
| INC | Increment memory | N, Z |
| INX | Increment X | N, Z |
| INY | Increment Y | N, Z |
| DEC | Decrement memory | N, Z |
| DEX | Decrement X | N, Z |
| DEY | Decrement Y | N, Z |

#### Branch (Relative addressing, +1 cycle if taken, +1 if page crossed)
| Mnemonic | Condition |
|----------|-----------|
| BCC | Carry clear (C=0) |
| BCS | Carry set (C=1) |
| BEQ | Zero set (Z=1) |
| BNE | Zero clear (Z=0) |
| BMI | Negative set (N=1) |
| BPL | Negative clear (N=0) |
| BVC | Overflow clear (V=0) |
| BVS | Overflow set (V=1) |

#### Jump & Subroutine
| Mnemonic | Description | Cycles |
|----------|-------------|--------|
| JMP abs | Jump to address | 3 |
| JMP ind | Jump indirect | 5 |
| JSR | Jump to subroutine (push PC+2) | 6 |
| RTS | Return from subroutine (pull PC, +1) | 6 |
| RTI | Return from interrupt (pull P, pull PC) | 6 |
| BRK | Software interrupt (push PC+2, push P) | 7 |

#### Stack
| Mnemonic | Description | Cycles |
|----------|-------------|--------|
| PHA | Push A to stack | 3 |
| PHP | Push P to stack (B and bit 5 set) | 3 |
| PLA | Pull A from stack | 4 |
| PLP | Pull P from stack (B and bit 5 ignored) | 4 |

#### Transfer
| Mnemonic | Description | Flags Affected |
|----------|-------------|----------------|
| TAX | A → X | N, Z |
| TAY | A → Y | N, Z |
| TXA | X → A | N, Z |
| TYA | Y → A | N, Z |
| TSX | SP → X | N, Z |
| TXS | X → SP | — |

#### Flag Operations
| Mnemonic | Description |
|----------|-------------|
| CLC | Clear carry |
| SEC | Set carry |
| CLD | Clear decimal |
| SED | Set decimal |
| CLI | Clear interrupt disable |
| SEI | Set interrupt disable |
| CLV | Clear overflow |

#### Other
| Mnemonic | Description |
|----------|-------------|
| NOP | No operation |

### Interrupt Vectors

| Vector | Address | Trigger |
|--------|---------|---------|
| **NMI** | $FFFA–$FFFB | Non-maskable interrupt — fired by PPU at start of VBlank (scanline 241) |
| **RESET** | $FFFC–$FFFD | Power-on / reset — CPU reads this to get initial PC |
| **IRQ/BRK** | $FFFE–$FFFF | Maskable interrupt (if I flag clear) / BRK instruction |

**Interrupt sequence** (NMI/IRQ):
1. Finish current instruction
2. Push PC high byte, then PC low byte to stack
3. Push P to stack (B=0 for IRQ/NMI, B=1 for BRK)
4. Set I flag (disable further IRQs)
5. Load PC from the appropriate vector

### Unofficial / Illegal Opcodes

Some games use undocumented 6502 opcodes. The most commonly needed ones:

| Opcode | Mnemonic | Description |
|--------|----------|-------------|
| $4B | ALR | AND + LSR |
| $0B | ANC | AND + set C from bit 7 |
| $6B | ARR | AND + ROR |
| $CB | AXS/SBX | (A AND X) - imm → X |
| $A7,$B7,$AF,$BF,$A3,$B3 | LAX | LDA + LDX |
| $87,$97,$8F,$83 | SAX | Store A AND X |
| $C7,$D7,$CF,$DF,$DB,$C3,$D3 | DCP | DEC + CMP |
| $E7,$F7,$EF,$FF,$FB,$E3,$F3 | ISC | INC + SBC |
| $07,$17,$0F,$1F,$1B,$03,$13 | SLO | ASL + ORA |
| $27,$37,$2F,$3F,$3B,$23,$33 | RLA | ROL + AND |
| $47,$57,$4F,$5F,$5B,$43,$53 | SRE | LSR + EOR |
| $67,$77,$6F,$7F,$7B,$63,$73 | RRA | ROR + ADC |

> **Recommendation:** Implement unofficial opcodes as needed. Start with the official set and add illegals when test ROMs or specific games require them.

---

## CPU Memory Map

This is the "switchboard" you implemented in the bus_read/bus_write functions. When the CPU accesses an address, this map tells you what it's actually talking to.

```
┌──────────────────────────────────────────┐
│ $0000–$07FF  Internal RAM (2 KB)         │ ← Zero Page ($0000–$00FF)
│              ├─ $0000–$00FF: Zero Page    │ ← Stack ($0100–$01FF)
│              ├─ $0100–$01FF: Stack        │ ← General purpose ($0200–$07FF)
│              └─ $0200–$07FF: RAM          │
├──────────────────────────────────────────┤
│ $0800–$1FFF  Mirrors of $0000–$07FF      │ ← addr & $07FF
│              (RAM repeats 3 times)        │
├──────────────────────────────────────────┤
│ $2000–$2007  PPU Registers (8 registers) │ ← See PPU section
├──────────────────────────────────────────┤
│ $2008–$3FFF  Mirrors of $2000–$2007      │ ← addr & $2007
│              (PPU regs repeat every 8 B)  │
├──────────────────────────────────────────┤
│ $4000–$4017  APU & I/O Registers         │ ← See APU & Controller sections
├──────────────────────────────────────────┤
│ $4018–$401F  APU & I/O (normally disabled)│ ← CPU test mode registers
├──────────────────────────────────────────┤
│ $4020–$FFFF  Cartridge Space             │
│              ├─ $4020–$5FFF: Expansion ROM│ ← Rarely used
│              ├─ $6000–$7FFF: SRAM (PRG RAM)│ ← Battery-backed save RAM
│              ├─ $8000–$BFFF: PRG ROM lower│ ← First 16 KB bank
│              └─ $C000–$FFFF: PRG ROM upper│ ← Last 16 KB bank (has vectors)
└──────────────────────────────────────────┘
```

### PPU Registers (CPU addresses $2000–$2007)

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $2000 | **PPUCTRL** | W | NMI enable, sprite size, BG/sprite pattern table addr, VRAM increment, nametable select |
| $2001 | **PPUMASK** | W | Color emphasis, sprite/BG enable, left-column clipping |
| $2002 | **PPUSTATUS** | R | VBlank flag, sprite 0 hit, sprite overflow |
| $2003 | **OAMADDR** | W | OAM address for $2004 access |
| $2004 | **OAMDATA** | R/W | OAM data read/write |
| $2005 | **PPUSCROLL** | W×2 | Fine scroll position (write X then Y) |
| $2006 | **PPUADDR** | W×2 | VRAM address (write high then low byte) |
| $2007 | **PPUDATA** | R/W | VRAM data read/write |
| $4014 | **OAMDMA** | W | OAM DMA — write page number, copies 256 bytes to OAM (takes 513–514 CPU cycles) |

#### PPUCTRL ($2000) bit layout
```
  7  6  5  4  3  2  1  0
  V  P  H  B  S  I  N  N
  │  │  │  │  │  │  └──┘── Nametable select (0=$2000, 1=$2400, 2=$2800, 3=$2C00)
  │  │  │  │  │  └──────── VRAM increment (0: +1 across, 1: +32 down)
  │  │  │  │  └─────────── Sprite pattern table (0: $0000, 1: $1000) (8×8 mode)
  │  │  │  └────────────── BG pattern table (0: $0000, 1: $1000)
  │  │  └───────────────── Sprite size (0: 8×8, 1: 8×16)
  │  └──────────────────── PPU master/slave (not used on NES)
  └─────────────────────── Generate NMI at VBlank (0: off, 1: on)
```

#### PPUMASK ($2001) bit layout
```
  7  6  5  4  3  2  1  0
  B  G  R  s  b  M  m  G
  │  │  │  │  │  │  │  └── Greyscale (0: normal, 1: greyscale)
  │  │  │  │  │  │  └───── Show BG in leftmost 8 pixels
  │  │  │  │  │  └──────── Show sprites in leftmost 8 pixels
  │  │  │  │  └─────────── Show background
  │  │  │  └────────────── Show sprites
  │  │  └───────────────── Emphasize red (green on PAL)
  │  └──────────────────── Emphasize green (red on PAL)
  └─────────────────────── Emphasize blue
```

#### PPUSTATUS ($2002) bit layout
```
  7  6  5  4  3  2  1  0
  V  S  O  .  .  .  .  .
  │  │  │  └──────────┘── Open bus (PPU latch)
  │  │  └───────────────── Sprite overflow (buggy on real HW)
  │  └──────────────────── Sprite 0 hit
  └─────────────────────── VBlank started (cleared on read, and at dot 1 of pre-render scanline)
```

### APU & I/O Registers (CPU addresses $4000–$4017)

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $4000 | **SQ1_VOL** | W | Pulse 1: duty, envelope, volume |
| $4001 | **SQ1_SWEEP** | W | Pulse 1: sweep control |
| $4002 | **SQ1_LO** | W | Pulse 1: timer low 8 bits |
| $4003 | **SQ1_HI** | W | Pulse 1: length counter + timer high 3 bits |
| $4004 | **SQ2_VOL** | W | Pulse 2: duty, envelope, volume |
| $4005 | **SQ2_SWEEP** | W | Pulse 2: sweep control |
| $4006 | **SQ2_LO** | W | Pulse 2: timer low 8 bits |
| $4007 | **SQ2_HI** | W | Pulse 2: length counter + timer high 3 bits |
| $4008 | **TRI_LINEAR** | W | Triangle: linear counter |
| $4009 | — | — | (unused) |
| $400A | **TRI_LO** | W | Triangle: timer low 8 bits |
| $400B | **TRI_HI** | W | Triangle: length counter + timer high 3 bits |
| $400C | **NOISE_VOL** | W | Noise: envelope, volume |
| $400D | — | — | (unused) |
| $400E | **NOISE_LO** | W | Noise: mode, period |
| $400F | **NOISE_HI** | W | Noise: length counter |
| $4010 | **DMC_FREQ** | W | DMC: IRQ enable, loop, frequency |
| $4011 | **DMC_RAW** | W | DMC: direct load (7-bit DAC) |
| $4012 | **DMC_START** | W | DMC: sample address = $C000 + (V × 64) |
| $4013 | **DMC_LEN** | W | DMC: sample length = (V × 16) + 1 bytes |
| $4014 | **OAMDMA** | W | OAM DMA (triggers DMA transfer) |
| $4015 | **SND_CHN** | R/W | Sound channel enable / status |
| $4016 | **JOY1** | R/W | Controller 1 (W: strobe, R: serial data) |
| $4017 | **JOY2 / FRAME** | R/W | Controller 2 read / APU frame counter control |

---

## PPU — Picture Processing Unit (2C02)

The PPU is the second major piece of your emulator. Don't start on this until your CPU passes nestest — but skimming this section now will help you understand what all those PPU register addresses ($2000–$2007) are for.

The PPU renders the video output: a 256×240 pixel frame at ~60 fps (NTSC). The CPU tells the PPU *what* to draw by writing to registers — the PPU does the actual work of turning tile data into pixels.

### Rendering Basics

- The screen is composed of **background tiles** (from nametables) and **sprites** (from OAM).
- Tiles are 8×8 pixels, stored in **pattern tables** as 2-bit-per-pixel data.
- Each pixel's color is determined by its 2-bit tile data + 2-bit attribute data = **4-bit palette index**.
- The NES palette has **64 unique colors** (not all visually distinct).

### PPU Timing (NTSC)

```
Scanline     Type           Dots (0–340)   Description
─────────────────────────────────────────────────────────────────
0–239        Visible        341 dots       Rendering scanlines (picture output)
240          Post-render    341 dots       Idle scanline
241          VBlank start   341 dots       VBlank flag set at dot 1; NMI triggered
242–260      VBlank         341 dots       CPU can safely access PPU
261 (or -1)  Pre-render     341 dots       VBlank cleared at dot 1; fetches for line 0
─────────────────────────────────────────────────────────────────
Total: 262 scanlines × 341 dots = 89,342 dots per frame
PPU clock = 5.369318 MHz → ~60.098 frames/sec (NTSC)
```

**Key timing points:**
- **VBlank NMI** fires at scanline 241, dot 1 (if NMI enabled in PPUCTRL)
- **Sprite 0 hit** is checked during visible scanlines when a non-transparent sprite 0 pixel overlaps a non-transparent BG pixel
- **OAM DMA** ($4014 write) halts the CPU for 513 or 514 cycles while copying 256 bytes to OAM
- The pre-render scanline (261) is 340 dots on even frames and 339 on odd frames (if rendering enabled) — this is the **odd frame cycle skip**

### Background Rendering

```
┌───────────────────────────────────────────────┐
│ Nametable (32×30 tiles = 960 bytes)           │
│ ┌─┬─┬─┬─┬─┬─┬─┬─┬─ ─ ─ ┬─┐                 │
│ │T│T│T│T│T│T│T│T│  ...  │T│  ← 32 tiles/row │
│ ├─┼─┼─┼─┼─┼─┼─┼─┤       ├─┤                 │
│ │T│T│T│T│T│T│T│T│  ...  │T│  × 30 rows       │
│ └─┴─┴─┴─┴─┴─┴─┴─┴─ ─ ─ ┴─┘                 │
│ + Attribute table (64 bytes) at end           │
│   Each byte covers a 4×4 tile (32×32 px) area│
│   providing 2-bit palette select per 2×2 tiles│
└───────────────────────────────────────────────┘
```

Each **nametable byte** is an index into the pattern table (which tile to draw).
The **attribute table** provides the upper 2 bits of the palette index for each 16×16 pixel area.

**Pattern table tile format** (16 bytes per tile):
```
Plane 0 (8 bytes):     Plane 1 (8 bytes):     Combined (2bpp):
  .#....#.                ..#...#.               .12...12.
  ##...##.                ..#...#.               .23...23.
  ........                ........               .........
  (etc.)                  (etc.)                 (etc.)

Bit 0 from Plane 0, Bit 1 from Plane 1 → 2-bit pixel value (0–3)
Value 0 = transparent (for sprites) or BG color
```

### Sprite Rendering (OAM)

- **OAM** (Object Attribute Memory): 256 bytes = 64 sprites × 4 bytes each
- Sprites can be **8×8** or **8×16** pixels (selected via PPUCTRL bit 5)

**OAM entry format (4 bytes per sprite):**

| Byte | Description |
|------|-------------|
| 0 | Y position (top of sprite, actual display = Y + 1) |
| 1 | Tile index (for 8×16: bit 0 selects pattern table, bits 7–1 = tile) |
| 2 | Attributes: `VHP--CC` — V=flip vertical, H=flip horizontal, P=priority (0=in front of BG), CC=palette |
| 3 | X position (left of sprite) |

**Sprite evaluation per scanline:**
1. PPU finds up to 8 sprites whose Y range overlaps the current scanline
2. If more than 8 found → sprite overflow flag set (buggy on real hardware)
3. Sprites are drawn in priority order (sprite 0 = highest priority)
4. **Sprite 0 Hit:** when a non-transparent pixel of sprite 0 overlaps a non-transparent BG pixel

### Scrolling

The PPU supports **fine scrolling** in both X and Y directions:
- **Coarse X/Y** (tile-level): encoded in the VRAM address register
- **Fine X**: stored in a separate register (written via $2005 first write)
- **Fine Y**: encoded in bits 12–14 of the VRAM address

Scrolling is controlled by writes to PPUSCROLL ($2005) and PPUADDR ($2006), which share an internal latch (the "loopy" registers: `v`, `t`, `x`, `w`):

```
Internal PPU registers (loopy):
  v: current VRAM address (15 bits) — used during rendering
  t: temporary VRAM address (15 bits) — holds scroll/address data
  x: fine X scroll (3 bits)
  w: write toggle (1 bit) — shared between $2005 and $2006

  v/t bit layout:
  yyy NN YYYYY XXXXX
  │││ ││ │││││ └───┘── Coarse X scroll (tile column, 0–31)
  │││ ││ └───┘──────── Coarse Y scroll (tile row, 0–29)
  │││ └┘────────────── Nametable select (2 bits)
  └┘┘───────────────── Fine Y scroll (3 bits)
```

> **Scrolling reference:** https://www.nesdev.org/wiki/PPU_scrolling — this is critical to get right for games like Super Mario Bros.

---

## PPU Memory Map

The PPU has its own separate address space (it's a different bus from the CPU). When you implement the PPU, you'll write a second `ppu_bus_read`/`ppu_bus_write` pair that maps these addresses.

```
┌──────────────────────────────────────────┐
│ $0000–$0FFF  Pattern Table 0 (4 KB)      │ ← CHR ROM/RAM (tiles for BG or sprites)
├──────────────────────────────────────────┤
│ $1000–$1FFF  Pattern Table 1 (4 KB)      │ ← CHR ROM/RAM (tiles for BG or sprites)
├──────────────────────────────────────────┤
│ $2000–$23FF  Nametable 0 (1 KB)          │ ← 960 tile bytes + 64 attribute bytes
├──────────────────────────────────────────┤
│ $2400–$27FF  Nametable 1 (1 KB)          │
├──────────────────────────────────────────┤
│ $2800–$2BFF  Nametable 2 (1 KB)          │ ← With only 2 KB VRAM, two of these
├──────────────────────────────────────────┤
│ $2C00–$2FFF  Nametable 3 (1 KB)          │   are mirrors (mirroring set by cartridge)
├──────────────────────────────────────────┤
│ $3000–$3EFF  Mirrors of $2000–$2EFF      │
├──────────────────────────────────────────┤
│ $3F00–$3F0F  Background palette (16 B)   │
│ $3F10–$3F1F  Sprite palette (16 B)       │
│   Note: $3F10/$3F14/$3F18/$3F1C mirror   │
│          $3F00/$3F04/$3F08/$3F0C          │
├──────────────────────────────────────────┤
│ $3F20–$3FFF  Mirrors of $3F00–$3F1F      │
├──────────────────────────────────────────┤
│ $4000–$FFFF  Mirrors of $0000–$3FFF      │
└──────────────────────────────────────────┘
```

### Nametable Mirroring

The NES has only **2 KB of VRAM** for nametables, but the address space has room for 4 nametables (4 KB). The cartridge controls which two physical pages the four logical nametables map to:

| Mirroring Mode | Nametable Layout | Common Use |
|----------------|------------------|------------|
| **Horizontal** | 0=A, 1=A, 2=B, 3=B | Vertical scrolling games |
| **Vertical** | 0=A, 1=B, 2=A, 3=B | Horizontal scrolling (e.g., Super Mario Bros) |
| **Single-screen** | All point to A or B | Special mapper configurations |
| **Four-screen** | 4 KB VRAM on cartridge | Rare, provides true 4 nametables |

---

## APU — Audio Processing Unit

Audio is the last major system to implement (Phase 8 in the roadmap). You don't need this until your emulator can play games with video. The APU is integrated into the 2A03 CPU chip and produces audio via 5 channels.

### Channel Overview

| Channel | Type | Output | Description |
|---------|------|--------|-------------|
| **Pulse 1** | Square wave | 4-bit DAC | Duty cycle (12.5%, 25%, 50%, 75%), sweep, envelope |
| **Pulse 2** | Square wave | 4-bit DAC | Same as Pulse 1 (different sweep behavior) |
| **Triangle** | Triangle wave | 4-bit DAC | No volume control, linear counter |
| **Noise** | Pseudorandom | 4-bit DAC | LFSR-based, short/long mode, envelope |
| **DMC** | Delta modulation | 7-bit DAC | Plays 1-bit delta-encoded samples from memory |

### APU Frame Counter

The frame counter clocks the envelope, sweep, and length counter units on the channels:

| Mode | Sequence | Rate |
|------|----------|------|
| **4-step** (default) | Steps 1–4, IRQ on step 4 | ~240 Hz per step |
| **5-step** | Steps 1–5, no IRQ | ~240 Hz per step |

```
4-step sequence (bit 7 of $4017 = 0):
Step 1: Envelope + Triangle linear counter
Step 2: Envelope + Triangle linear counter + Length counter + Sweep
Step 3: Envelope + Triangle linear counter
Step 4: Envelope + Triangle linear counter + Length counter + Sweep + IRQ

5-step sequence (bit 7 of $4017 = 1):
Step 1: Envelope + Triangle linear counter
Step 2: Envelope + Triangle linear counter + Length counter + Sweep
Step 3: Envelope + Triangle linear counter
Step 4: (nothing)
Step 5: Envelope + Triangle linear counter + Length counter + Sweep
```

### Length Counter Table

Used by pulse, triangle, and noise channels. Loaded from the high 5 bits of registers $4003/$4007/$400B/$400F:

```
Index: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
Value: 10 254 20  2 40  4 80  6 160  8 60 10 14 12 26 14

Index: 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
Value: 12 16 24 18 48 20 96 22 192 24 72 26 16 28 32 30
```

---

## iNES ROM Format

You'll need this early — right after the CPU works, you'll parse ROM files to load games. The format is simple: a 16-byte header followed by the actual game data.

### Header Format (16 bytes)

```
Offset  Size  Description
──────────────────────────────────────────────
0–3     4     Constant: "NES" followed by MS-DOS EOF ($4E $45 $53 $1A)
4       1     PRG ROM size in 16 KB units
5       1     CHR ROM size in 8 KB units (0 = uses CHR RAM)
6       1     Flags 6: Mapper low nibble, mirroring, battery, trainer
7       1     Flags 7: Mapper high nibble, VS/Playchoice, NES 2.0 indicator
8       1     Flags 8: PRG RAM size (rarely used)
9       1     Flags 9: TV system (rarely used)
10      1     Flags 10: TV system, PRG RAM (rarely used, unofficial)
11–15   5     Padding (should be zero)
```

### Flags 6 detail
```
  7  6  5  4  3  2  1  0
  M  M  M  M  F  T  B  M
  │  │  │  │  │  │  │  └── Mirroring (0: horizontal, 1: vertical)
  │  │  │  │  │  │  └───── Battery-backed PRG RAM at $6000–$7FFF
  │  │  │  │  │  └──────── 512-byte trainer at $7000–$71FF (before PRG ROM)
  │  │  │  │  └─────────── Four-screen VRAM
  └──┴──┴──┘────────────── Lower nibble of mapper number
```

### Flags 7 detail
```
  7  6  5  4  3  2  1  0
  M  M  M  M  2  2  V  V
  │  │  │  │  │  │  └──┘── VS Unisystem / PlayChoice-10
  │  │  │  │  └──┘──────── If equal to 2: NES 2.0 format
  └──┴──┴──┘────────────── Upper nibble of mapper number
```

**Mapper number** = (Flags7 & $F0) | (Flags6 >> 4)

### ROM Layout After Header

```
[16-byte header]
[512-byte trainer, if present]
[PRG ROM: N × 16384 bytes]
[CHR ROM: N × 8192 bytes]
```

---

## Mappers

Mappers are extra hardware on the game cartridge that lets games be bigger than the NES's address space normally allows. They control which ROM banks appear in the CPU/PPU address space. There are 256+ mapper numbers, but a handful cover the vast majority of games. Start with Mapper 0 (no switching at all), and add others as you want to play more games.

### Mapper 0 — NROM

The simplest mapper. No bank switching at all.

| Variant | PRG ROM | CHR ROM |
|---------|---------|---------|
| NROM-128 | 16 KB at $8000 (mirrored at $C000) | 8 KB |
| NROM-256 | 32 KB at $8000–$FFFF | 8 KB |

**Games:** Super Mario Bros, Donkey Kong, Balloon Fight, Ice Climber, Excitebike

### Mapper 1 — MMC1 (SxROM)

Serial-load register mapper. Writes are done 1 bit at a time (5 writes to fill a register).

- PRG ROM: switchable 16 KB or 32 KB banks
- CHR ROM: switchable 4 KB or 8 KB banks
- Mirroring control: horizontal, vertical, or single-screen

**Games:** Legend of Zelda, Metroid, Mega Man 2, Final Fantasy, Dragon Warrior

### Mapper 2 — UxROM

Simple bank-switched PRG ROM. CHR RAM (no CHR ROM).

- $8000–$BFFF: switchable 16 KB PRG bank
- $C000–$FFFF: fixed to last 16 KB PRG bank
- 8 KB CHR RAM

**Games:** Mega Man, Castlevania, Contra, Duck Tales, Metal Gear

### Mapper 3 — CNROM

Simple bank-switched CHR ROM. Fixed PRG ROM.

- 16 KB or 32 KB PRG ROM (no switching)
- CHR ROM: switchable 8 KB banks

**Games:** Solomon's Key, Arkanoid, Gradius, Paperboy

### Mapper 4 — MMC3 (TxROM)

The most sophisticated common mapper. Features:

- PRG ROM: two switchable 8 KB banks + two fixed 8 KB banks
- CHR ROM: six switchable banks (2 × 2 KB + 4 × 1 KB)
- Scanline counter for IRQ (used for split-screen effects)
- Mirroring control

**Games:** Super Mario Bros 2 & 3, Kirby's Adventure, Mega Man 3–6, Batman

> **Implementation priority:** Start with Mapper 0, then add 1, 2, 4 for maximum game compatibility.

---

## Controllers

Controller input is straightforward — it's one of the easier parts of the emulator. You'll add this once you have basic PPU rendering working and want to actually play a game.

### Reading Controllers

The CPU communicates with controllers through $4016 (controller 1) and $4017 (controller 2).

**Protocol:**
1. Write $01 then $00 to $4016 (strobe)
2. Read $4016 eight times — each read returns one button state (bit 0)

**Button read order:**

| Read # | Button |
|--------|--------|
| 1 | A |
| 2 | B |
| 3 | Select |
| 4 | Start |
| 5 | Up |
| 6 | Down |
| 7 | Left |
| 8 | Right |

After 8 reads, further reads return 1 (open bus behavior varies).

### Recommended Key Mapping

| NES Button | Keyboard | SDL2 Gamepad |
|------------|----------|--------------|
| A | Z | A / Cross |
| B | X | B / Circle |
| Select | Right Shift | Back |
| Start | Enter | Start |
| Up | ↑ | D-Pad Up |
| Down | ↓ | D-Pad Down |
| Left | ← | D-Pad Left |
| Right | → | D-Pad Right |

---

# Part 3 — Plan & Resources

## Development Roadmap

This is the order that worked. Each phase builds on the last, and each had clear criteria for "done" before moving on. Phases 1–8 are complete; remaining work is mappers, polish, and accuracy ROMs.

### Phase 1 — CPU (6502 core) — ✅ done

- [x] Implement all official 6502 instructions (56 instructions, all addressing modes)
- [x] Cycle-accurate execution (correct cycle counts per instruction)
- [x] Status flag handling (N, V, B, D, I, Z, C)
- [x] Stack operations ($0100–$01FF)
- [x] Interrupt handling (NMI, IRQ, RESET) — 7-cycle service cost on both NMI and IRQ
- [x] CPU logging (format compatible with nestest.log for verification)

### Phase 2 — iNES ROM Loading + Mapper 0 — ✅ done

- [x] Parse iNES header (detect PRG/CHR size, mapper number, mirroring)
- [x] Load PRG ROM into CPU address space ($8000–$FFFF)
- [x] Load CHR ROM into PPU pattern tables ($0000–$1FFF)
- [x] Implement Mapper 0 (NROM-128 and NROM-256)
- [x] PAL detection (header byte 9 bit 0 + filename heuristic for badly-flagged ROMs)

### Phase 3 — Validate CPU with Test ROMs — ✅ done

- [x] Run **nestest.nes** (automation mode at $C000) — compares output against nestest.log line-by-line
- [x] Fix all CPU bugs caught by nestest
- [x] Add unofficial opcodes nestest exercises

### Phase 4 — PPU — ✅ done

- [x] Implement PPU registers ($2000–$2007, PPUSTATUS read side-effects, w toggle, $2007 read buffer)
- [x] Pattern table reading (CHR ROM → tile decoding)
- [x] Nametable rendering (background tiles)
- [x] Attribute table (palette selection per 2×2 tile area)
- [x] Palette RAM with $3F10/14/18/1C mirror folding
- [x] Sprite evaluation, fetch, and rendering (OAM, OAM Y +1 delay, priority)
- [x] OAM DMA ($4014) — 513/514 cycle CPU stall
- [x] VBlank / NMI timing (PPU is the sole NMI source; APU's fake-NMI placeholder removed)
- [x] Sprite 0 hit detection (all five hardware conditions)
- [x] Sprite overflow flag
- [x] Odd-frame pre-render dot skip
- [x] Render 256×240 framebuffer to SDL2 window via streaming texture, scaled by `SDL_RenderSetLogicalSize`

### Phase 5 — Simple Games — ✅ done

- [x] **Donkey Kong** — Mapper 0, no scroll; plays correctly
- [x] Controller input via SDL2 keyboard
- [x] Debug rendering against Mesen as reference

### Phase 6 — Scrolling — ✅ done

- [x] Loopy `v`/`t`/`x`/`w` internal registers
- [x] PPUSCROLL ($2005) writes — first/second write toggle
- [x] PPUADDR ($2006) interaction with `v`/`t`
- [x] Mid-frame scroll changes via the sprite-0-hit polling + $2005/$2006 pattern
- [x] `copy_hori_v_from_t` at dot 257; `copy_vert_v_from_t` at pre-render dots 280–304

### Phase 7 — Super Mario Bros — ✅ done

- [x] Horizontal scrolling with nametable switching
- [x] Sprite-0-hit-driven status bar split with no vertical misalignment after the OAM-Y delay fix

### Phase 8 — APU (Audio) — ✅ done

- [x] Pulse channels (duty cycle, sweep, envelope)
- [x] Triangle channel (linear counter)
- [x] Noise channel (LFSR, envelope)
- [x] DMC channel (sample reader, output unit, IRQ on sample end)
- [x] Frame counter (4-step and 5-step modes, IRQ inhibit)
- [x] Audio mixing per nesdev formulas, high-pass + low-pass filter chain
- [x] Audio output via SDL2 audio callback + ring buffer
- [x] Ring-buffer back-pressure as the wall-clock throttle (AUDIO_SYNC timing mode)

### Phase 9 — More Mappers — ⏳ pending

- [ ] **Mapper 1** (MMC1) — serial register, bank switching, mirroring control
- [ ] **Mapper 2** (UxROM) — switchable PRG, CHR RAM
- [ ] **Mapper 3** (CNROM) — switchable CHR ROM
- [ ] **Mapper 4** (MMC3) — bank switching + scanline IRQ (PPU-A12 watcher)
- [ ] Test each mapper with appropriate games

### Phase 10 — Accuracy & Tricky Games — ⏳ pending

- [ ] Run blargg's `cpu_instrs` test suite
- [ ] Run `ppu_vbl_nmi` test ROMs (NMI / vblank-flag timing)
- [ ] Run `ppu_sprite_hit` and `ppu_sprite_overflow` test ROMs
- [ ] Sub-instruction PPU sync for `$2002` reads (eliminates the residual SMB split-line jitter that catch-up emulators have)
- [ ] **Battletoads** — notoriously timing-sensitive
- [ ] Reference: https://www.nesdev.org/wiki/Tricky-to-emulate_games

### Phase 11 — Polish & Extras — ⏳ pending

- [ ] Save states (serialize full emulator state)
- [ ] Battery-backed SRAM (save to file at $6000–$7FFF)
- [ ] Second controller
- [ ] Step-debugger UI in the debug window (memory viewer, breakpoints)
- [ ] Rewind / fast-forward
- [ ] Configuration file (key bindings, scaling, etc.)
- [ ] Performance pass on the debug window (text-rendering cache; currently each text draw creates and destroys a texture)

---

## Test ROMs & Debugging

### Essential Test ROMs

| Test ROM | Tests | Priority |
|----------|-------|----------|
| **nestest.nes** | All official + many unofficial CPU opcodes | **Start here** |
| **blargg's cpu_instrs** | Individual CPU instruction tests (11 sub-tests) | High |
| **blargg's ppu_vbl_nmi** | VBlank and NMI timing | High |
| **blargg's sprite_hit_tests** | Sprite 0 hit detection | Medium |
| **blargg's sprite_overflow_tests** | Sprite overflow flag | Medium |
| **blargg's apu_test** | APU channel and timing tests | For Phase 8 |
| **blargg's oam_read** | OAM read behavior | Medium |
| **blargg's cpu_timing_test** | Cycle-accurate CPU timing | High |

> Test ROMs index: https://www.nesdev.org/wiki/Emulator_tests

### Debugging Strategy

1. **CPU Logging:** Output a log line per instruction (PC, opcode, operand, A, X, Y, P, SP, cycle count). Compare against nestest.log or Mesen trace logs.
2. **Visual Debugging:** Render pattern tables and nametables in separate debug windows.
3. **Reference Emulator:** Use **Mesen** (https://www.mesen.ca/) — its debugger lets you step through instructions, view PPU state, memory, tiles, and sprites side-by-side.
4. **Breakpoints:** Implement address-based breakpoints in your CPU loop for targeted debugging.
5. **Automated Testing:** Write scripts that run test ROMs and check the result byte at a known memory address.

### Common Pitfalls

- **Off-by-one on stack:** SP points to the *next free slot*, not the last pushed value. Push: write then decrement SP. Pull: increment SP then read.
- **BRK pushes PC+2:** Not PC+1. There's a padding byte after BRK.
- **NMI vs IRQ timing:** NMI is edge-sensitive, IRQ is level-sensitive.
- **PPU address latch ($2005/$2006 sharing):** Reading $2002 resets the write toggle.
- **OAM DMA timing:** 513 cycles (even CPU cycle) or 514 cycles (odd CPU cycle).
- **Sprite 0 hit:** Doesn't trigger at X=255 or if both BG and sprites are disabled.

---

## Reference Links

### Primary References (Essential)

| Resource | URL | Description |
|----------|-----|-------------|
| **NESDev Wiki** | https://www.nesdev.org/wiki/Nesdev_Wiki | The single most important resource — covers everything |
| **NES Documentation PDF** | https://www.nesdev.org/NESDoc.pdf | Compact full-system reference document |
| **6502 Instruction Set** | https://www.masswerk.at/6502/6502_instruction_set.html | Complete opcode table with cycle counts and flag effects |
| **6502.org** | http://6502.org/ | The 6502 microprocessor resource hub |

### Detailed Hardware References

| Resource | URL | Description |
|----------|-----|-------------|
| **CPU Reference** | https://www.nesdev.org/wiki/CPU | CPU registers, timing, interrupts |
| **PPU Reference** | https://www.nesdev.org/wiki/PPU | PPU registers, rendering pipeline |
| **PPU Scrolling** | https://www.nesdev.org/wiki/PPU_scrolling | Critical: loopy register behavior |
| **PPU Rendering** | https://www.nesdev.org/wiki/PPU_rendering | Dot-by-dot rendering breakdown |
| **APU Reference** | https://www.nesdev.org/wiki/APU | Audio channels, frame counter, mixing |
| **APU Mixer** | https://www.nesdev.org/wiki/APU_Mixer | How to mix the 5 channels |
| **iNES Format** | https://www.nesdev.org/wiki/INES | ROM header format specification |
| **Mapper List** | https://www.nesdev.org/wiki/Mapper | All mapper numbers and descriptions |
| **Controller** | https://www.nesdev.org/wiki/Controller_reading | Controller protocol details |

### Tutorials & Guides

| Resource | URL | Description |
|----------|-----|-------------|
| **NES Emu Overview** | https://yizhang82.dev/nes-emu-overview | Excellent blog series on writing a NES emulator in C++ |
| **NES Emu Main Loop** | https://yizhang82.dev/nes-emu-main-loop | CPU/PPU synchronization |
| **NES Emu CPU** | https://yizhang82.dev/nes-emu-cpu | 6502 CPU emulation details |
| **NES Ebook (Rust)** | https://bugzmanov.github.io/nes_ebook/ | Step-by-step NES emulator guide (concepts apply to any language) |
| **Nerdy Nights** | https://nerdy-nights.nes.science/ | NES programming tutorials (great for understanding hardware) |

### Tools & Emulators

| Resource | URL | Description |
|----------|-----|-------------|
| **Mesen** | https://www.mesen.ca/ | Excellent NES emulator with powerful debugger |
| **FCEUX** | https://fceux.com/ | Feature-rich NES emulator with debugging tools |
| **SDL2** | https://www.libsdl.org/ | Cross-platform multimedia library |
| **Visual 6502** | http://www.visual6502.org/ | Transistor-level 6502 simulation |
| **6502 Online Assembler** | https://www.masswerk.at/6502/assembler.html | Write and test 6502 assembly online |
| **6502 Disassembler** | https://www.masswerk.at/6502/disassembler.html | Online 6502 disassembler |

### Test ROMs

| Resource | URL | Description |
|----------|-----|-------------|
| **Test ROM Index** | https://www.nesdev.org/wiki/Emulator_tests | Comprehensive list of all test ROMs |
| **Tricky Games** | https://www.nesdev.org/wiki/Tricky-to-emulate_games | Games known to stress emulator accuracy |

### Reference Emulator Source Code

| Project | URL | Language | Notes |
|---------|-----|----------|-------|
| **neschan** | https://github.com/yizhang82/neschan | C++ | Clean C++ + SDL, good reference for this project |
| **SimpleNES** | https://github.com/amhndu/SimpleNES | C++ | Compact and readable C++ emulator |
| **nes-emulator** | https://github.com/fogleman/nes | Go | Well-structured, good architecture reference |
| **LaiNES** | https://github.com/AndreaOrru/LaiNES | C++ | Compact cycle-accurate emulator |

### Books

| Title | Author | Description |
|-------|--------|-------------|
| **I Am Error** | Nathan Altice | Deep technical history of the NES platform |

---

## Quick-Start Checklist

When you're ready to start coding, here's the order of operations:

1. **Set up the project** — CMake + SDL2 skeleton, open a window, render a pixel buffer
2. **Read the CPU wiki page** — understand all 6502 instructions and addressing modes
3. **Implement the CPU** — instruction decode, execute, flag updates, cycle counting
4. **Load a ROM** — parse iNES header, map PRG/CHR into memory
5. **Run nestest.nes** — boot at $C000, compare logs line by line until all pass
6. **Add PPU** — registers, tile rendering, VBlank/NMI
7. **Render to screen** — SDL2 texture from pixel buffer, show background
8. **Add sprites + controller** — play Donkey Kong!
9. **Add scrolling** — play Super Mario Bros!
10. **Add APU** — hear the music
11. **Add mappers** — play more games

---

*Last updated: March 2026*
