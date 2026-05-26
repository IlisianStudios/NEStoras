#pragma once
#ifndef NESTORAS_PPU_H
#define NESTORAS_PPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CPU;
typedef struct CPU CPU;

// ---------- Output framebuffer (256x240 ARGB8888) -----------------------
#define PPU_FRAME_W 256
#define PPU_FRAME_H 240
extern uint32_t ppu_framebuffer[PPU_FRAME_W * PPU_FRAME_H];

// ---------- Lifecycle ---------------------------------------------------
void ppu_init(void);
void ppu_reset(void);

// ---------- CPU-side register I/O (called from bus.c) -------------------
uint8_t ppu_register_read(uint16_t addr);
void    ppu_register_write(uint16_t addr, uint8_t data);
void    ppu_oamdma(uint8_t page, CPU *cpu);

// ---------- Stepping ----------------------------------------------------
// Advance the PPU by one dot. The CPU loop calls this 3 times per CPU
// cycle on NTSC (and a 4th time every 5th cycle on PAL).
void ppu_step(CPU *cpu);

// ---------- Frame readiness --------------------------------------------
bool ppu_frame_ready(void);
void ppu_clear_frame_ready(void);

// ---------- Debug snapshot ---------------------------------------------
typedef struct {
    uint32_t frame;
    int16_t  scanline;
    int16_t  dot;
    uint16_t v, t;
    uint8_t  x, w;
    uint8_t  ctrl, mask, status;
    uint8_t  oam_addr;
    bool     nmi_line;
    bool     sprite0_hit;
    bool     sprite_overflow;
    bool     in_vblank;
    uint32_t nmi_count;
    uint32_t frame_count;
} PPUDebug;
extern PPUDebug ppu_dbg;

// Side-effect-free read for the debug window (CHR + nametable viewers).
uint8_t ppu_bus_read_debug(uint16_t addr);

#ifdef __cplusplus
}
#endif
#endif // NESTORAS_PPU_H
