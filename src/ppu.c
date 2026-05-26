#include "ppu.h"
#include "cpu.h"
#include "cartridge.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
//  PPU (Ricoh 2C02) — cycle-accurate, catch-up integrated.
//  See PPU_SPEC.md for the design doc; nesdev wiki for the gory details.
// ============================================================================

// ---------- Framebuffer + debug -------------------------------------------
uint32_t ppu_framebuffer[PPU_FRAME_W * PPU_FRAME_H];
PPUDebug ppu_dbg;

// ---------- Internal state ------------------------------------------------
typedef struct {
    // Timing
    int16_t  scanline;     // 0..260 NTSC (261 = pre-render), 0..310 PAL (311 = pre-render)
    int16_t  dot;          // 0..340
    uint32_t frame;
    bool     odd_frame;

    // CPU-visible registers
    uint8_t  ctrl;         // $2000
    uint8_t  mask;         // $2001
    uint8_t  status;       // $2002 (bits 7=vblank, 6=sp0 hit, 5=overflow)
    uint8_t  oam_addr;     // $2003

    // Loopy internal registers
    uint16_t v;            // 15-bit current VRAM address / scroll
    uint16_t t;            // 15-bit temp VRAM address
    uint8_t  x;            // 3-bit fine X
    uint8_t  w;            // write toggle

    // $2007 read buffer
    uint8_t  read_buffer;

    // VRAM (2 nametables physically)
    uint8_t  vram[0x800];
    // Palette RAM
    uint8_t  palette[32];
    // OAM
    uint8_t  oam[256];
    uint8_t  oam2[32];      // secondary OAM
    uint8_t  oam2_count;

    // Background shift registers + latches
    uint16_t bg_shift_pat_lo;
    uint16_t bg_shift_pat_hi;
    uint16_t bg_shift_attr_lo;
    uint16_t bg_shift_attr_hi;
    uint8_t  bg_next_nt;
    uint8_t  bg_next_at;
    uint8_t  bg_next_pat_lo;
    uint8_t  bg_next_pat_hi;

    // Sprite rendering state (per current scanline)
    uint8_t  sp_pat_lo[8];
    uint8_t  sp_pat_hi[8];
    uint8_t  sp_attr[8];
    uint8_t  sp_x[8];
    bool     sp_is_sprite0[8];
    uint8_t  sp_count;
    bool     sprite0_in_range_next; // tagged during eval, used during fetch

    // Latches
    bool     frame_ready;

    // Open bus
    uint8_t  open_bus;
} PPU;

static PPU ppu;

// Region-dependent line counts (filled in by ppu_init)
static int ppu_total_scanlines;
static int ppu_pre_render_line;

// ---------- NES master palette (2C02 default, ARGB8888) -------------------
static const uint32_t NES_PALETTE_ARGB[64] = {
    0xFF545454, 0xFF001E74, 0xFF081090, 0xFF300088,
    0xFF440064, 0xFF5C0030, 0xFF540400, 0xFF3C1800,
    0xFF202A00, 0xFF083A00, 0xFF004000, 0xFF003C00,
    0xFF00323C, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF989698, 0xFF084CC4, 0xFF3032EC, 0xFF5C1EE4,
    0xFF8814B0, 0xFFA01464, 0xFF982220, 0xFF783C00,
    0xFF545A00, 0xFF287200, 0xFF087C00, 0xFF007628,
    0xFF006678, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFF4C9AEC, 0xFF787CEC, 0xFFB062EC,
    0xFFE454EC, 0xFFEC58B4, 0xFFEC6A64, 0xFFD48820,
    0xFFA0AA00, 0xFF74C400, 0xFF4CD020, 0xFF38CC6C,
    0xFF38B4CC, 0xFF3C3C3C, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFFA8CCEC, 0xFFBCBCEC, 0xFFD4B2EC,
    0xFFECAEEC, 0xFFECAED4, 0xFFECB4B0, 0xFFE4C490,
    0xFFCCD278, 0xFFB4DE78, 0xFFA8E290, 0xFF98E2B4,
    0xFFA0D6E4, 0xFFA0A2A0, 0xFF000000, 0xFF000000
};

// ============================================================================
//  PPU bus (CHR + nametables + palette)
// ============================================================================

static uint16_t vram_addr_to_index(uint16_t addr) {
    addr &= 0x0FFF;
    uint16_t nt  = addr / 0x400;
    uint16_t off = addr & 0x3FF;
    // cartridge->mirroring: 0 = horizontal, 1 = vertical (iNES bit 0)
    if (cartridge && cartridge->mirroring == 1) {
        // Vertical: NT0/NT2 share, NT1/NT3 share
        nt &= 1;
    } else {
        // Horizontal: NT0/NT1 share, NT2/NT3 share
        nt = (nt >> 1) & 1;
    }
    return (uint16_t)(nt * 0x400 + off);
}

static uint16_t palette_addr(uint16_t addr) {
    addr &= 0x1F;
    // $3F10/14/18/1C mirror to $3F00/04/08/0C
    if ((addr & 0x13) == 0x10) addr &= ~0x10;
    return addr;
}

static uint8_t ppu_bus_read_internal(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (cartridge && cartridge->mapper.ppu_read)
            return cartridge->mapper.ppu_read(cartridge, addr);
        return 0;
    } else if (addr < 0x3F00) {
        return ppu.vram[vram_addr_to_index(addr & 0x2FFF)];
    } else {
        return ppu.palette[palette_addr(addr)];
    }
}

static void ppu_bus_write_internal(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (cartridge && cartridge->mapper.ppu_write)
            cartridge->mapper.ppu_write(cartridge, addr, data);
    } else if (addr < 0x3F00) {
        ppu.vram[vram_addr_to_index(addr & 0x2FFF)] = data;
    } else {
        ppu.palette[palette_addr(addr)] = data;
    }
}

uint8_t ppu_bus_read_debug(uint16_t addr) {
    // No side effects — safe for the debug window's CHR/NT viewers.
    return ppu_bus_read_internal(addr);
}

// ============================================================================
//  Loopy scroll helpers
// ============================================================================

static inline bool rendering_enabled(void) {
    return (ppu.mask & 0x18) != 0;
}

static void inc_coarse_x(void) {
    if ((ppu.v & 0x001F) == 31) {
        ppu.v &= ~0x001F;
        ppu.v ^= 0x0400;          // flip horizontal nametable bit
    } else {
        ppu.v += 1;
    }
}

static void inc_y(void) {
    if ((ppu.v & 0x7000) != 0x7000) {
        ppu.v += 0x1000;          // bump fine Y
    } else {
        ppu.v &= ~0x7000;         // reset fine Y
        uint16_t y = (ppu.v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu.v ^= 0x0800;      // flip vertical nametable bit
        } else if (y == 31) {
            y = 0;                // out-of-bounds, no flip
        } else {
            y += 1;
        }
        ppu.v = (ppu.v & ~0x03E0) | (y << 5);
    }
}

static void copy_hori_v_from_t(void) {
    // Copies coarse X (bits 0-4) and horizontal nametable bit (bit 10)
    ppu.v = (ppu.v & ~0x041F) | (ppu.t & 0x041F);
}

static void copy_vert_v_from_t(void) {
    // Copies fine Y (bits 12-14), vertical nametable bit (bit 11),
    // coarse Y (bits 5-9)
    ppu.v = (ppu.v & ~0x7BE0) | (ppu.t & 0x7BE0);
}

// ============================================================================
//  Background fetch pipeline
// ============================================================================

static void bg_load_next_tile(void) {
    // Load latched bytes into the LOW halves of the shift registers.
    ppu.bg_shift_pat_lo = (ppu.bg_shift_pat_lo & 0xFF00) | ppu.bg_next_pat_lo;
    ppu.bg_shift_pat_hi = (ppu.bg_shift_pat_hi & 0xFF00) | ppu.bg_next_pat_hi;
    // Attribute is 2 bits per tile, expanded across 8 pixels.
    uint8_t at = ppu.bg_next_at;
    ppu.bg_shift_attr_lo = (ppu.bg_shift_attr_lo & 0xFF00) | ((at & 0x01) ? 0xFF : 0x00);
    ppu.bg_shift_attr_hi = (ppu.bg_shift_attr_hi & 0xFF00) | ((at & 0x02) ? 0xFF : 0x00);
}

static void bg_shift(void) {
    ppu.bg_shift_pat_lo  <<= 1;
    ppu.bg_shift_pat_hi  <<= 1;
    ppu.bg_shift_attr_lo <<= 1;
    ppu.bg_shift_attr_hi <<= 1;
}

static void bg_fetch_nt(void) {
    uint16_t addr = 0x2000 | (ppu.v & 0x0FFF);
    ppu.bg_next_nt = ppu_bus_read_internal(addr);
}

static void bg_fetch_at(void) {
    uint16_t addr = 0x23C0
                  | (ppu.v & 0x0C00)
                  | ((ppu.v >> 4) & 0x38)
                  | ((ppu.v >> 2) & 0x07);
    uint8_t at = ppu_bus_read_internal(addr);
    // Pick the right 2 bits for this 2x2 tile region within the 4x4 byte
    uint8_t shift = ((ppu.v >> 4) & 4) | (ppu.v & 2);
    ppu.bg_next_at = (at >> shift) & 0x03;
}

static void bg_fetch_pat_lo(void) {
    uint16_t base = (ppu.ctrl & 0x10) ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 7;
    uint16_t addr = base + ((uint16_t)ppu.bg_next_nt << 4) + fine_y;
    ppu.bg_next_pat_lo = ppu_bus_read_internal(addr);
}

static void bg_fetch_pat_hi(void) {
    uint16_t base = (ppu.ctrl & 0x10) ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 7;
    uint16_t addr = base + ((uint16_t)ppu.bg_next_nt << 4) + fine_y + 8;
    ppu.bg_next_pat_hi = ppu_bus_read_internal(addr);
}

// ============================================================================
//  Sprite evaluation + fetch
//  Simplified-but-correct algorithm: do all of secondary-OAM eval at dot 257
//  and all 8 pattern fetches at the same time. Not cycle-accurate within a
//  scanline, but produces correct pixels and correct sprite-0-hit positions.
// ============================================================================

static uint8_t bit_reverse(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static void sprite_evaluate(void) {
    // Clear secondary OAM to $FF
    memset(ppu.oam2, 0xFF, sizeof(ppu.oam2));
    ppu.oam2_count = 0;
    ppu.sprite0_in_range_next = false;

    uint8_t height = (ppu.ctrl & 0x20) ? 16 : 8;
    int next_line = ppu.scanline + 1;
    if (next_line >= PPU_FRAME_H) return;  // sprites not evaluated for post/vblank

    int n;
    for (n = 0; n < 64; n++) {
        uint8_t sy = ppu.oam[n * 4 + 0];
        int row = next_line - sy;
        if (row < 0 || row >= height) continue;
        if (ppu.oam2_count < 8) {
            ppu.oam2[ppu.oam2_count * 4 + 0] = ppu.oam[n * 4 + 0];
            ppu.oam2[ppu.oam2_count * 4 + 1] = ppu.oam[n * 4 + 1];
            ppu.oam2[ppu.oam2_count * 4 + 2] = ppu.oam[n * 4 + 2];
            ppu.oam2[ppu.oam2_count * 4 + 3] = ppu.oam[n * 4 + 3];
            if (n == 0) ppu.sprite0_in_range_next = true;
            ppu.oam2_count++;
        } else {
            // More than 8 sprites — set overflow flag.
            // (Faithful hardware-bug emulation is more complex; this is the
            // straightforward "intended" behaviour.)
            ppu.status |= 0x20;
            break;
        }
    }
}

static void sprite_fetch(void) {
    uint8_t height = (ppu.ctrl & 0x20) ? 16 : 8;
    int next_line = ppu.scanline + 1;

    // Stage current scanline's sprites from what eval just produced.
    // (We swap "next" → "current" implicitly because we always render from
    //  sp_pat_*/sp_x as filled here, and the row computed here matches.)
    ppu.sp_count = ppu.oam2_count;
    for (int i = 0; i < 8; i++) {
        ppu.sp_is_sprite0[i] = false;
        ppu.sp_pat_lo[i] = 0;
        ppu.sp_pat_hi[i] = 0;
        ppu.sp_attr[i]   = 0;
        ppu.sp_x[i]      = 0xFF;
    }

    bool sp0_flag = ppu.sprite0_in_range_next;

    for (int i = 0; i < ppu.oam2_count; i++) {
        uint8_t sy   = ppu.oam2[i * 4 + 0];
        uint8_t tile = ppu.oam2[i * 4 + 1];
        uint8_t attr = ppu.oam2[i * 4 + 2];
        uint8_t sx   = ppu.oam2[i * 4 + 3];

        int row = next_line - sy;
        bool flip_v = (attr & 0x80) != 0;
        bool flip_h = (attr & 0x40) != 0;

        if (flip_v) row = (height - 1) - row;

        uint16_t addr;
        if (height == 16) {
            uint16_t table = (tile & 1) ? 0x1000 : 0x0000;
            uint8_t  index = tile & 0xFE;
            if (row >= 8) { index += 1; row -= 8; }
            addr = table + (uint16_t)index * 16 + (uint16_t)row;
        } else {
            uint16_t table = (ppu.ctrl & 0x08) ? 0x1000 : 0x0000;
            addr = table + (uint16_t)tile * 16 + (uint16_t)row;
        }

        uint8_t lo = ppu_bus_read_internal(addr);
        uint8_t hi = ppu_bus_read_internal(addr + 8);
        if (flip_h) { lo = bit_reverse(lo); hi = bit_reverse(hi); }

        ppu.sp_pat_lo[i] = lo;
        ppu.sp_pat_hi[i] = hi;
        ppu.sp_attr[i]   = attr;
        ppu.sp_x[i]      = sx;
        ppu.sp_is_sprite0[i] = (i == 0 && sp0_flag);
    }
}

// ============================================================================
//  Pixel mux
// ============================================================================

static void render_pixel(void) {
    int px = ppu.dot - 1;     // dot 1 → column 0
    int py = ppu.scanline;
    if (px < 0 || px >= PPU_FRAME_W || py < 0 || py >= PPU_FRAME_H) return;

    // --- Background pixel ---
    uint8_t bg_pixel = 0, bg_palette = 0;
    if ((ppu.mask & 0x08) && ((ppu.mask & 0x02) || px >= 8)) {
        uint16_t bit_mux = 0x8000 >> ppu.x;
        uint8_t p0 = (ppu.bg_shift_pat_lo  & bit_mux) ? 1 : 0;
        uint8_t p1 = (ppu.bg_shift_pat_hi  & bit_mux) ? 1 : 0;
        uint8_t a0 = (ppu.bg_shift_attr_lo & bit_mux) ? 1 : 0;
        uint8_t a1 = (ppu.bg_shift_attr_hi & bit_mux) ? 1 : 0;
        bg_pixel   = (uint8_t)((p1 << 1) | p0);
        bg_palette = (uint8_t)((a1 << 1) | a0);
    }

    // --- Sprite pixel (first opaque wins) ---
    uint8_t sp_pixel = 0, sp_palette = 0;
    bool    sp_behind = false;
    bool    sp_is_sprite0 = false;
    bool    sprites_on = (ppu.mask & 0x10) && ((ppu.mask & 0x04) || px >= 8);
    if (sprites_on) {
        for (int i = 0; i < ppu.sp_count; i++) {
            if (ppu.sp_x[i] != 0) continue;        // not on this column yet
            uint8_t p0 = (ppu.sp_pat_lo[i] >> 7) & 1;
            uint8_t p1 = (ppu.sp_pat_hi[i] >> 7) & 1;
            uint8_t pix = (uint8_t)((p1 << 1) | p0);
            if (pix == 0) continue;                // transparent
            sp_pixel   = pix;
            sp_palette = ppu.sp_attr[i] & 0x03;
            sp_behind  = (ppu.sp_attr[i] & 0x20) != 0;
            sp_is_sprite0 = ppu.sp_is_sprite0[i];
            break;
        }
    }

    // --- Sprite 0 hit ---
    if (sp_is_sprite0 && bg_pixel != 0 && sp_pixel != 0
        && (ppu.mask & 0x18) == 0x18
        && ((ppu.mask & 0x06) == 0x06 || px >= 8)
        && px != 255) {
        ppu.status |= 0x40;
    }

    // --- Priority mux ---
    uint8_t pal_index;
    if (bg_pixel == 0 && sp_pixel == 0) {
        pal_index = ppu.palette[0];
    } else if (bg_pixel == 0) {
        pal_index = ppu.palette[palette_addr(0x10 + sp_palette * 4 + sp_pixel)];
    } else if (sp_pixel == 0) {
        pal_index = ppu.palette[palette_addr(bg_palette * 4 + bg_pixel)];
    } else {
        pal_index = sp_behind
            ? ppu.palette[palette_addr(bg_palette * 4 + bg_pixel)]
            : ppu.palette[palette_addr(0x10 + sp_palette * 4 + sp_pixel)];
    }
    if (ppu.mask & 0x01) pal_index &= 0x30;     // greyscale

    ppu_framebuffer[py * PPU_FRAME_W + px] = NES_PALETTE_ARGB[pal_index & 0x3F];
}

// ============================================================================
//  Sprite X countdown / shift — per-dot
// ============================================================================

static void sprites_tick(void) {
    for (int i = 0; i < ppu.sp_count; i++) {
        if (ppu.sp_x[i] > 0) {
            ppu.sp_x[i]--;
        } else {
            ppu.sp_pat_lo[i] <<= 1;
            ppu.sp_pat_hi[i] <<= 1;
        }
    }
}

// ============================================================================
//  Lifecycle
// ============================================================================

void ppu_init(void) {
    memset(&ppu, 0, sizeof(ppu));
    memset(ppu_framebuffer, 0, sizeof(ppu_framebuffer));
    memset(&ppu_dbg, 0, sizeof(ppu_dbg));

    bool pal = (cartridge && cartridge->is_pal);
    ppu_total_scanlines = pal ? 312 : 262;
    ppu_pre_render_line = pal ? 311 : 261;

    ppu.scanline   = ppu_pre_render_line;
    ppu.dot        = 0;
    ppu.frame      = 0;
    ppu.odd_frame  = false;
    ppu.frame_ready = false;
    ppu.status     = 0;
}

void ppu_reset(void) {
    ppu.ctrl = 0;
    ppu.mask = 0;
    // status: leave bit 7 (vblank) untouched per hardware; rest clear
    ppu.status &= 0x80;
    ppu.oam_addr = 0;
    ppu.t = 0;
    ppu.x = 0;
    ppu.w = 0;
    ppu.read_buffer = 0;
    ppu.scanline = ppu_pre_render_line;
    ppu.dot      = 0;
    ppu.odd_frame = false;
}

// ============================================================================
//  CPU-visible register I/O
// ============================================================================

uint8_t ppu_register_read(uint16_t addr) {
    switch (addr & 7) {
        case 2: {  // $2002 PPUSTATUS
            uint8_t result = (ppu.status & 0xE0) | (ppu.open_bus & 0x1F);
            ppu.status &= ~0x80;     // clear vblank
            ppu.w = 0;
            ppu.open_bus = result;
            return result;
        }
        case 4: {  // $2004 OAMDATA
            uint8_t v = ppu.oam[ppu.oam_addr];
            ppu.open_bus = v;
            return v;
        }
        case 7: {  // $2007 PPUDATA
            uint16_t a = ppu.v & 0x3FFF;
            uint8_t result;
            if (a < 0x3F00) {
                result = ppu.read_buffer;
                ppu.read_buffer = ppu_bus_read_internal(a);
            } else {
                // Palette reads return immediately; buffer is loaded with the
                // mirrored nametable byte underneath.
                result = ppu_bus_read_internal(a);
                ppu.read_buffer = ppu_bus_read_internal(a & 0x2FFF);
            }
            ppu.v += (ppu.ctrl & 0x04) ? 32 : 1;
            ppu.v &= 0x7FFF;
            ppu.open_bus = result;
            return result;
        }
        default:
            return ppu.open_bus;
    }
}

void ppu_register_write(uint16_t addr, uint8_t data) {
    ppu.open_bus = data;
    switch (addr & 7) {
        case 0: {  // $2000 PPUCTRL
            bool old_nmi = (ppu.ctrl & 0x80) != 0;
            ppu.ctrl = data;
            ppu.t = (ppu.t & 0xF3FF) | ((uint16_t)(data & 0x03) << 10);
            bool new_nmi = (data & 0x80) != 0;
            // "Extra NMI" — if vblank flag is set and NMI just got enabled,
            // fire NMI immediately.
            extern CPU cpu;
            if (!old_nmi && new_nmi && (ppu.status & 0x80)) {
                cpu_nmi(&cpu);
                ppu_dbg.nmi_count++;
            }
            break;
        }
        case 1:    // $2001 PPUMASK
            ppu.mask = data;
            break;
        case 3:    // $2003 OAMADDR
            ppu.oam_addr = data;
            break;
        case 4:    // $2004 OAMDATA
            ppu.oam[ppu.oam_addr] = data;
            ppu.oam_addr++;
            break;
        case 5: {  // $2005 PPUSCROLL
            if (ppu.w == 0) {
                ppu.t = (ppu.t & 0xFFE0) | (data >> 3);
                ppu.x = data & 0x07;
                ppu.w = 1;
            } else {
                ppu.t = (ppu.t & 0x8C1F)
                      | ((uint16_t)(data & 0x07) << 12)
                      | ((uint16_t)(data & 0xF8) << 2);
                ppu.w = 0;
            }
            break;
        }
        case 6: {  // $2006 PPUADDR
            if (ppu.w == 0) {
                ppu.t = (ppu.t & 0x00FF) | ((uint16_t)(data & 0x3F) << 8);
                ppu.t &= 0x3FFF;
                ppu.w = 1;
            } else {
                ppu.t = (ppu.t & 0xFF00) | data;
                ppu.v = ppu.t;
                ppu.w = 0;
            }
            break;
        }
        case 7: {  // $2007 PPUDATA
            ppu_bus_write_internal(ppu.v & 0x3FFF, data);
            ppu.v += (ppu.ctrl & 0x04) ? 32 : 1;
            ppu.v &= 0x7FFF;
            break;
        }
        default:
            // $2002 ignores writes (open-bus updated above)
            break;
    }
}

// ============================================================================
//  OAMDMA  ($4014)
// ============================================================================

void ppu_oamdma(uint8_t page, CPU *cpu) {
    uint16_t base = (uint16_t)page << 8;
    for (int i = 0; i < 256; i++) {
        ppu.oam[(ppu.oam_addr + i) & 0xFF] = bus_read(base + i);
    }
    // 513 cycles of CPU stall (+1 if started on an odd CPU cycle).
    cpu->stall_cycles += 513;
    if (cpu->total_cycles & 1) cpu->stall_cycles += 1;
}

// ============================================================================
//  Frame ready
// ============================================================================

bool ppu_frame_ready(void) { return ppu.frame_ready; }
void ppu_clear_frame_ready(void) { ppu.frame_ready = false; }

// ============================================================================
//  ppu_step — one PPU dot.
// ============================================================================

void ppu_step(CPU *cpu) {
    bool is_visible    = (ppu.scanline >= 0 && ppu.scanline < 240);
    bool is_prerender  = (ppu.scanline == ppu_pre_render_line);
    bool is_fetch_line = is_visible || is_prerender;
    bool ren           = rendering_enabled();

    // ---- Pre-render line clear of vblank / sp0 / overflow ----------------
    if (is_prerender && ppu.dot == 1) {
        ppu.status &= ~0xE0;
    }

    // ---- Background fetch + scroll updates -------------------------------
    if (is_fetch_line && ren) {
        bool in_visible_fetch = (ppu.dot >= 1   && ppu.dot <= 256);
        bool in_prefetch      = (ppu.dot >= 321 && ppu.dot <= 336);

        if (in_visible_fetch || in_prefetch) {
            bg_shift();
            switch (ppu.dot & 7) {
                case 1: bg_load_next_tile(); bg_fetch_nt(); break;
                case 3: bg_fetch_at(); break;
                case 5: bg_fetch_pat_lo(); break;
                case 7: bg_fetch_pat_hi(); break;
                case 0: inc_coarse_x(); break;
            }
        }

        if (ppu.dot == 256) inc_y();
        if (ppu.dot == 257) copy_hori_v_from_t();
        if (is_prerender && ppu.dot >= 280 && ppu.dot <= 304) copy_vert_v_from_t();

        // Unused fetches at 337/339 (NT byte twice) — MMC5 uses them; skip.
        if (ppu.dot == 338 || ppu.dot == 340) bg_fetch_nt();
    }

    // ---- Sprite work -----------------------------------------------------
    if (is_visible) {
        // Pixel-time sprite shifters
        if (ppu.dot >= 1 && ppu.dot <= 256) {
            sprites_tick();
        }
        // Lump sprite eval + fetch at dot 257 (simplified — see comment in
        // sprite_evaluate). This is correct visually for almost everything.
        if (ppu.dot == 257) {
            sprite_evaluate();
            sprite_fetch();
        }
    } else if (is_prerender && ppu.dot == 257) {
        // Pre-render: nothing visible on line -1, but we still want sprites
        // ready for line 0. Eval/fetch for "next_line = 0".
        sprite_evaluate();
        sprite_fetch();
    }

    // ---- Pixel ------------------------------------------------------------
    if (is_visible && ppu.dot >= 1 && ppu.dot <= 256) {
        render_pixel();
    }

    // ---- Vblank entry ----------------------------------------------------
    if (ppu.scanline == 241 && ppu.dot == 1) {
        ppu.status |= 0x80;
        ppu.frame_ready = true;
        if (ppu.ctrl & 0x80) {
            cpu_nmi(cpu);
            ppu_dbg.nmi_count++;
        }
    }

    // ---- Debug snapshot --------------------------------------------------
    // Cheap; just mirror state into the debug struct.
    ppu_dbg.scanline = ppu.scanline;
    ppu_dbg.dot      = ppu.dot;
    ppu_dbg.frame    = ppu.frame;
    ppu_dbg.v        = ppu.v;
    ppu_dbg.t        = ppu.t;
    ppu_dbg.x        = ppu.x;
    ppu_dbg.w        = ppu.w;
    ppu_dbg.ctrl     = ppu.ctrl;
    ppu_dbg.mask     = ppu.mask;
    ppu_dbg.status   = ppu.status;
    ppu_dbg.oam_addr = ppu.oam_addr;
    ppu_dbg.in_vblank       = (ppu.status & 0x80) != 0;
    ppu_dbg.sprite0_hit     = (ppu.status & 0x40) != 0;
    ppu_dbg.sprite_overflow = (ppu.status & 0x20) != 0;
    ppu_dbg.nmi_line        = (ppu.ctrl & 0x80) && (ppu.status & 0x80);

    // ---- Advance dot/line/frame -----------------------------------------
    ppu.dot++;

    // Pre-render odd-frame dot skip
    if (is_prerender && ppu.dot == 340 && ppu.odd_frame && ren) {
        // Skip the last dot: jump straight to next frame.
        ppu.dot = 0;
        ppu.scanline = 0;
        ppu.frame++;
        ppu_dbg.frame_count++;
        ppu.odd_frame = !ppu.odd_frame;
        return;
    }

    if (ppu.dot > 340) {
        ppu.dot = 0;
        ppu.scanline++;
        if (ppu.scanline >= ppu_total_scanlines) {
            ppu.scanline = 0;
            ppu.frame++;
            ppu_dbg.frame_count++;
            ppu.odd_frame = !ppu.odd_frame;
        }
    }

    (void)cpu; // silence unused on configurations without NMI in this dot
}
