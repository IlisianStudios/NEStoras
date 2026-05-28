#include "debug_window.h"
#include "apu.h"
#include "cpu.h"
#include "bus.h"
#include "ppu.h"
#include "ringbuffer.h"
#include "cartridge.h"

#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define DBG_WIN_W    520
#define DBG_WIN_H    960
#define FONT_SIZE        14
#define FONT_SIZE_SMALL  10
#define LINE_H           15
#define LINE_H_SMALL     12
#define PAD_X        12
#define PAD_Y        10

#define LOG_MAX_LINES 128
#define LOG_LINE_LEN  96

// Volume bars — drawn on their own slim row below the channel text
#define BAR_FULL_W    (DBG_WIN_W - PAD_X * 2)
#define BAR_SLIM_H    4

// State section height — must match render_state() exactly:
//   CPU(3 lines +6)  APU(1+rate_line +4)
//   3 channels(line+bar+gap4)  last channel(+6 extra)
//   Audio hdr(1)  Audio data(1)  Audio bar(+gap4+6)
//   Controller(1+6)  Cartridge(1+6)  Separator(4)
//   Footer(2 lines + PAD_Y)
#define STATE_H  ((3*LINE_H+6) + (2*LINE_H+4) \
                 + 3*(LINE_H+BAR_SLIM_H+4) + (LINE_H+BAR_SLIM_H+4+6) \
                 + LINE_H + LINE_H + (BAR_SLIM_H+4+6) \
                 + (LINE_H+6) + (LINE_H+6) + 4 \
                 + LINE_H + LINE_H + PAD_Y)
#define STATE_Y         (DBG_WIN_H - STATE_H)

// Waveform section — 6 graphs between log and state (P1 P2 TR NO DM MX)
#define WAVE_COUNT      6
#define WAVE_H          24
#define WAVE_GAP        3
#define WAVE_LABEL_W    20
#define WAVE_GRAPH_W    (BAR_FULL_W - WAVE_LABEL_W)
#define WAVE_SECTION_H  (LINE_H + 4 + WAVE_COUNT * (WAVE_H + WAVE_GAP))
#define WAVE_Y          (STATE_Y - WAVE_SECTION_H - 4)

// Log: shrunk to ~10 lines to make room for the PPU panel.
#define LOG_VISIBLE     10
#define LOG_AREA_H      (PAD_Y + LINE_H + LOG_VISIBLE * LINE_H + 4)

// PPU panel — between log and waveforms.
//   Header + 3 state lines + sp0/ovf/NMI line  (4 × LINE_H + 6 spacer)
//   "Sprites" heading                          (LINE_H + 2 spacer)
//   Visual sprite grid (8×8 tiles at 3× scale) (SPR_GRID_H = 192)
#define PPU_Y           LOG_AREA_H

#define SPR_SCALE       3
#define SPR_CELL_W      (8 * SPR_SCALE)               // 24
#define SPR_CELL_H      (8 * SPR_SCALE)               // 24
#define SPR_GRID_COLS   8
#define SPR_GRID_ROWS   8
#define SPR_GRID_W      (SPR_GRID_COLS * SPR_CELL_W)  // 192
#define SPR_GRID_H      (SPR_GRID_ROWS * SPR_CELL_H)  // 192
#define SPR_LABEL_W     24                            // row-label gutter width

#define PPU_SECTION_H   (4*LINE_H + 6 + LINE_H + 2 + SPR_GRID_H)

// ---------- colour palette ----------------------------------------------

static const SDL_Color COL_BG      = {  24,  24,  32, 255 };
static const SDL_Color COL_HEADING = { 100, 200, 255, 255 };
static const SDL_Color COL_LABEL   = { 180, 180, 180, 255 };
static const SDL_Color COL_VALUE   = { 255, 255, 200, 255 };
static const SDL_Color COL_ON      = {  80, 255, 100, 255 };
static const SDL_Color COL_OFF     = { 255,  70,  70, 255 };
static const SDL_Color COL_DIM     = { 100, 100, 100, 255 };
static const SDL_Color COL_LOG     = { 200, 200, 200, 255 };
static const SDL_Color COL_SEP     = {  60,  60,  80, 255 };

static char  log_lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int   log_head  = 0;   // next write index
static int   log_count = 0;   // total stored (capped at LOG_MAX_LINES)

static SDL_Window   *main_win     = NULL;
static SDL_Window   *dbg_win      = NULL;
static SDL_Renderer *dbg_renderer = NULL;
static TTF_Font     *dbg_font     = NULL;
static TTF_Font     *dbg_font_small = NULL;   // for the OAM table
static Uint32        dbg_win_id   = 0;
static bool          dbg_visible  = false;
static bool         *testing_mode_ptr = NULL;  // points to cpu.testing_mode

static uint32_t      last_draw_tick = 0;
static const uint32_t DRAW_INTERVAL_MS = 16;   // ~60 fps
static bool          show_paused = false;

// Sprite-viewer cache. Re-render a sprite cell only when its OAM bytes,
// the relevant ctrl bits, or the sprite-palette region of palette RAM have
// changed since the last debug frame.
static SDL_Texture  *sprite_tex = NULL;
static uint32_t      sprite_pixels[SPR_GRID_W * SPR_GRID_H];
static uint8_t       sprite_cache_oam[256];
static uint8_t       sprite_cache_palette[16];   // sprite half of palette RAM
static uint8_t       sprite_cache_ctrl = 0xFF;   // forces first-frame redraw
static bool          sprite_cache_inited = false;

// CHR (pattern table) viewer cache. Two 128×128 textures, one per pattern
// table ($0000 + $1000). Re-rasterised only when the bytes we sample from
// CHR differ from last frame (cheap heuristic that catches mapper bank
// switches and CHR-RAM writes without hashing 8 KiB every frame).
static SDL_Texture  *chr_tex[2]   = { NULL, NULL };
static uint32_t      chr_pixels[2][128 * 128];
static uint8_t       chr_sample[32];
static bool          chr_cache_inited = false;

// No snapshot buffer needed — render_waveforms reads directly from apu_dbg scope buffers.

// ---------- font search -------------------------------------------------

static const char *font_candidates[] = {
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "C:\\Windows\\Fonts\\consola.ttf",
    NULL
};

static TTF_Font *open_mono_font(int size) {
    for (int i = 0; font_candidates[i]; i++) {
        TTF_Font *f = TTF_OpenFont(font_candidates[i], size);
        if (f) return f;
    }
    return NULL;
}

// ---------- drawing helpers ---------------------------------------------

static void draw_text_f(int x, int y, const char *text, SDL_Color col, TTF_Font *font) {
    if (!text || !text[0] || !font) return;
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(dbg_renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(dbg_renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void draw_text(int x, int y, const char *text, SDL_Color col) {
    draw_text_f(x, y, text, col, dbg_font);
}

static void draw_text_small(int x, int y, const char *text, SDL_Color col) {
    draw_text_f(x, y, text, col, dbg_font_small);
}

static void draw_bar(int x, int y, int w, int h, float pct, SDL_Color col) {
    SDL_SetRenderDrawColor(dbg_renderer, 40, 40, 50, 255);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(dbg_renderer, &bg);
    int fw = (int)(pct * (float)w);
    if (fw > w) fw = w;
    if (fw < 0) fw = 0;
    SDL_SetRenderDrawColor(dbg_renderer, col.r, col.g, col.b, col.a);
    SDL_Rect fg = { x, y, fw, h };
    SDL_RenderFillRect(dbg_renderer, &fg);
}

static void position_beside_main(void) {
    if (!main_win || !dbg_win) return;
    int mx, my;
    SDL_GetWindowPosition(main_win, &mx, &my);
    int dx = mx - DBG_WIN_W;
    if (dx < 0) dx = 0;
    SDL_SetWindowPosition(dbg_win, dx, my);
}

void debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_lines[log_head], LOG_LINE_LEN, fmt, args);
    va_end(args);
    log_head = (log_head + 1) % LOG_MAX_LINES;
    if (log_count < LOG_MAX_LINES) log_count++;
}

void debug_window_set_paused(bool paused) {
    show_paused = paused;
}

bool debug_window_init(SDL_Window *main_window, bool *testing_mode) {
    main_win = main_window;
    testing_mode_ptr = testing_mode;

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    dbg_font = open_mono_font(FONT_SIZE);
    if (!dbg_font) {
        fprintf(stderr, "debug_window: no monospaced font found\n");
        TTF_Quit();
        return false;
    }
    dbg_font_small = open_mono_font(FONT_SIZE_SMALL);
    if (!dbg_font_small) {
        // Non-fatal — fall back to main font if the small one fails.
        dbg_font_small = dbg_font;
    }

    dbg_win = SDL_CreateWindow(
        "NEStoras Debug",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        DBG_WIN_W, DBG_WIN_H,
        SDL_WINDOW_HIDDEN
    );
    if (!dbg_win) {
        fprintf(stderr, "debug_window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_CloseFont(dbg_font); dbg_font = NULL;
        return false;
    }
    dbg_win_id = SDL_GetWindowID(dbg_win);

    // No vsync — the debug window has its own self-throttle (DRAW_INTERVAL_MS).
    // Vsync here would block the main loop's CPU/APU/PPU stepping inside
    // SDL_RenderPresent every frame, starving the audio ring buffer.
    dbg_renderer = SDL_CreateRenderer(dbg_win, -1, SDL_RENDERER_ACCELERATED);
    if (!dbg_renderer) {
        fprintf(stderr, "debug_window: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(dbg_win); dbg_win = NULL;
        TTF_CloseFont(dbg_font);   dbg_font = NULL;
        TTF_Quit();
        return false;
    }

    sprite_tex = SDL_CreateTexture(dbg_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SPR_GRID_W, SPR_GRID_H);
    if (sprite_tex) {
        // Initial fill with the cell-background colour
        for (int i = 0; i < SPR_GRID_W * SPR_GRID_H; i++) {
            sprite_pixels[i] = 0xFF202028;
        }
        SDL_UpdateTexture(sprite_tex, NULL, sprite_pixels, SPR_GRID_W * (int)sizeof(uint32_t));
    }

    for (int t = 0; t < 2; t++) {
        chr_tex[t] = SDL_CreateTexture(dbg_renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            128, 128);
    }

    printf("Debug window ready (press D to toggle)\n");
    return true;
}

void debug_window_destroy(void) {
    if (sprite_tex)   { SDL_DestroyTexture(sprite_tex);    sprite_tex = NULL; }
    for (int t = 0; t < 2; t++) {
        if (chr_tex[t]) { SDL_DestroyTexture(chr_tex[t]); chr_tex[t] = NULL; }
    }
    if (dbg_renderer) { SDL_DestroyRenderer(dbg_renderer); dbg_renderer = NULL; }
    if (dbg_win)      { SDL_DestroyWindow(dbg_win);        dbg_win = NULL; }
    // dbg_font_small may equal dbg_font if the small-size open failed —
    // only close it if it's a distinct font, else the second close would
    // double-free.
    if (dbg_font_small && dbg_font_small != dbg_font) {
        TTF_CloseFont(dbg_font_small);
    }
    dbg_font_small = NULL;
    if (dbg_font)     { TTF_CloseFont(dbg_font);           dbg_font = NULL; }
    TTF_Quit();
}

void debug_window_toggle(void) {
    if (!dbg_win) return;
    dbg_visible = !dbg_visible;
    if (dbg_visible) {
        position_beside_main();
        SDL_ShowWindow(dbg_win);
        apu_dbg.enabled = true;
        if (testing_mode_ptr) *testing_mode_ptr = true;
        debug_log("--- testing mode enabled ---");
    } else {
        SDL_HideWindow(dbg_win);
        apu_dbg.enabled = false;
        if (testing_mode_ptr) *testing_mode_ptr = false;
    }
}

bool debug_window_visible(void) {
    return dbg_visible;
}

bool debug_window_handle_event(const SDL_Event *event) {
    if (!dbg_win) return false;
    if (event->type == SDL_WINDOWEVENT &&
        event->window.windowID == dbg_win_id &&
        event->window.event == SDL_WINDOWEVENT_CLOSE) {
        dbg_visible = false;
        SDL_HideWindow(dbg_win);
        apu_dbg.enabled = false;
        if (testing_mode_ptr) *testing_mode_ptr = false;
        return true;
    }
    return false;
}

// Draw one channel's scrolling scope directly from apu_dbg's circular buffer.
// scope_pos is the next-write column (oldest data is at scope_pos, newest just before it).
static void draw_scope(int x, int y, int w, int h,
                       const float *scope, int scope_pos, SDL_Color col) {
    SDL_SetRenderDrawColor(dbg_renderer, 28, 28, 40, 255);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(dbg_renderer, &bg);

    SDL_SetRenderDrawColor(dbg_renderer, col.r, col.g, col.b, col.a);
    int px0 = x, py0 = y + h - 1;
    for (int i = 0; i < APU_SCOPE_W; i++) {
        float sv = scope[(scope_pos + i) & (APU_SCOPE_W - 1)];
        if (sv > 1.0f) sv = 1.0f;
        if (sv < 0.0f) sv = 0.0f;
        int px = x + (int)((long long)i * (w - 1) / (APU_SCOPE_W - 1));
        int py = y + h - 1 - (int)(sv * (float)(h - 1));
        if (i > 0)
            SDL_RenderDrawLine(dbg_renderer, px0, py0, px, py);
        px0 = px; py0 = py;
    }
}

static void render_waveforms(void) {
    static const char *labels[] = { "P1", "P2", "TR", "NO", "DM", "MX" };
    static const SDL_Color cols[] = {
        {  80, 255, 100, 255 },  // P1: green
        { 100, 200, 255, 255 },  // P2: cyan
        { 255, 200,  80, 255 },  // TR: amber
        { 255, 100, 200, 255 },  // NO: pink
        { 255, 140,  60, 255 },  // DM: orange
        { 220, 220, 220, 255 },  // MX: white
    };
    const float *scopes[WAVE_COUNT] = {
        apu_dbg.scope_p1,  apu_dbg.scope_p2,  apu_dbg.scope_tri,
        apu_dbg.scope_noi, apu_dbg.scope_dmc, apu_dbg.scope_mix,
    };

    draw_text(PAD_X, WAVE_Y, "Waveforms", COL_HEADING);

    int y = WAVE_Y + LINE_H + 4;
    for (int ch = 0; ch < WAVE_COUNT; ch++) {
        draw_text(PAD_X, y + (WAVE_H - LINE_H) / 2, labels[ch], cols[ch]);
        draw_scope(PAD_X + WAVE_LABEL_W, y, WAVE_GRAPH_W, WAVE_H,
                   scopes[ch], apu_dbg.scope_pos, cols[ch]);
        y += WAVE_H + WAVE_GAP;
    }
}

static void render_log(void) {
    draw_text(PAD_X, PAD_Y, "--- Log ---", COL_HEADING);

    int y = PAD_Y + LINE_H;
    int max_vis = LOG_VISIBLE;
    int skip = log_count > max_vis ? log_count - max_vis : 0;

    for (int i = skip; i < log_count; i++) {
        int idx = (log_head - log_count + i + LOG_MAX_LINES) % LOG_MAX_LINES;
        draw_text(PAD_X, y, log_lines[idx], COL_LOG);
        y += LINE_H;
    }
}

static void render_separator(void) {
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, PAD_X, LOG_AREA_H, DBG_WIN_W - PAD_X, LOG_AREA_H);
    // Separator below the PPU panel
    SDL_RenderDrawLine(dbg_renderer, PAD_X, PPU_Y + PPU_SECTION_H,
                       DBG_WIN_W - PAD_X, PPU_Y + PPU_SECTION_H);
}

// Rasterise one OAM sprite as an 8×8 tile, 3× nearest-neighbour, into the
// sprite_pixels buffer at the sprite's grid position. Honours horizontal
// and vertical flip from the attribute byte. In 8×16 mode shows only the
// top half (the first tile of the pair) — the grid budget doesn't include
// room for 8×16 cells, and 8×8 is what the games we test use anyway.
static void render_sprite_into_grid(int sprite_idx) {
    const uint8_t *oam = ppu_oam_view();
    uint8_t tile = oam[sprite_idx * 4 + 1];
    uint8_t attr = oam[sprite_idx * 4 + 2];

    bool flip_h = (attr & 0x40) != 0;
    bool flip_v = (attr & 0x80) != 0;
    uint8_t pal_idx = attr & 0x03;

    uint16_t addr;
    if (ppu_dbg.ctrl & 0x20) {
        // 8×16 mode — table selected by tile bit 0, tile index masked to even
        uint16_t table = (tile & 1) ? 0x1000 : 0x0000;
        uint8_t  index = tile & 0xFE;
        addr = table + (uint16_t)index * 16;
    } else {
        // 8×8 mode — pattern table from ctrl bit 3
        uint16_t table = (ppu_dbg.ctrl & 0x08) ? 0x1000 : 0x0000;
        addr = table + (uint16_t)tile * 16;
    }

    int col = sprite_idx % SPR_GRID_COLS;
    int row = sprite_idx / SPR_GRID_COLS;
    int gx  = col * SPR_CELL_W;
    int gy  = row * SPR_CELL_H;

    // Two background tints distinguish sprite 0's cell.
    uint32_t transparent_bg = (sprite_idx == 0) ? 0xFF003860 : 0xFF202028;

    for (int sy = 0; sy < 8; sy++) {
        int src_row = flip_v ? (7 - sy) : sy;
        uint8_t lo = ppu_bus_read_debug(addr + (uint16_t)src_row);
        uint8_t hi = ppu_bus_read_debug(addr + (uint16_t)src_row + 8);
        for (int sx = 0; sx < 8; sx++) {
            int src_col = flip_h ? (7 - sx) : sx;
            uint8_t bit = (uint8_t)(7 - src_col);
            uint8_t pix = (uint8_t)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
            uint32_t argb = (pix == 0) ? transparent_bg
                                       : ppu_sprite_color(pal_idx, pix);
            // Scale the 8×8 source up to SPR_SCALE × SPR_SCALE per source pixel.
            for (int dy = 0; dy < SPR_SCALE; dy++) {
                int py = gy + sy * SPR_SCALE + dy;
                uint32_t *row_p = &sprite_pixels[py * SPR_GRID_W + gx + sx * SPR_SCALE];
                for (int dx = 0; dx < SPR_SCALE; dx++) {
                    row_p[dx] = argb;
                }
            }
        }
    }
}

// Cache-aware sprite-grid renderer. Detects which sprite cells need
// re-rasterising by comparing OAM/ctrl/sprite-palette against last frame,
// re-renders only those cells into the pixel buffer, then uploads the
// whole texture once and blits it. If nothing changed, we skip the upload
// and just blit the texture that's already on the GPU.
static void render_sprite_grid(int x, int y) {
    if (!sprite_tex) return;

    const uint8_t *oam = ppu_oam_view();
    const uint8_t *pal = ppu_palette_view();

    bool dirty[64];
    memset(dirty, 0, sizeof(dirty));
    bool all_dirty = false;

    // First frame: render everything.
    if (!sprite_cache_inited) {
        all_dirty = true;
        sprite_cache_inited = true;
    }

    // Sprite-size bit (ctrl bit 5) and 8×8 sprite pattern-table bit
    // (ctrl bit 3) both affect what gets drawn for every sprite.
    if ((ppu_dbg.ctrl & 0x28) != (sprite_cache_ctrl & 0x28)) {
        all_dirty = true;
    }
    sprite_cache_ctrl = ppu_dbg.ctrl;

    // Any change in the sprite palette region invalidates every cell's colours.
    for (int i = 0; i < 16; i++) {
        if (pal[0x10 + i] != sprite_cache_palette[i]) {
            all_dirty = true;
            sprite_cache_palette[i] = pal[0x10 + i];
        }
    }

    int dirty_count = 0;
    if (all_dirty) {
        for (int i = 0; i < 64; i++) dirty[i] = true;
        // refresh the OAM cache mirror too so per-byte compare next frame works
        memcpy(sprite_cache_oam, oam, sizeof(sprite_cache_oam));
        dirty_count = 64;
    } else {
        for (int i = 0; i < 64; i++) {
            bool changed = false;
            for (int j = 0; j < 4; j++) {
                if (oam[i * 4 + j] != sprite_cache_oam[i * 4 + j]) {
                    changed = true;
                    sprite_cache_oam[i * 4 + j] = oam[i * 4 + j];
                }
            }
            if (changed) {
                dirty[i] = true;
                dirty_count++;
            }
        }
    }

    if (dirty_count > 0) {
        for (int i = 0; i < 64; i++) {
            if (dirty[i]) render_sprite_into_grid(i);
        }
        SDL_UpdateTexture(sprite_tex, NULL, sprite_pixels,
                          SPR_GRID_W * (int)sizeof(uint32_t));
    }

    // Blit and overlay
    SDL_Rect dst = { x, y, SPR_GRID_W, SPR_GRID_H };
    SDL_RenderCopy(dbg_renderer, sprite_tex, NULL, &dst);

    // Cyan outline around sprite 0's cell
    SDL_SetRenderDrawColor(dbg_renderer, 100, 200, 255, 255);
    SDL_Rect sp0_outline = { x, y, SPR_CELL_W, SPR_CELL_H };
    SDL_RenderDrawRect(dbg_renderer, &sp0_outline);

    // Row labels in the gutter (sprite # of the leftmost sprite per row)
    char buf[8];
    for (int r = 0; r < SPR_GRID_ROWS; r++) {
        snprintf(buf, sizeof(buf), "%02X", r * SPR_GRID_COLS);
        draw_text_small(x - SPR_LABEL_W + 2,
                        y + r * SPR_CELL_H + (SPR_CELL_H - LINE_H_SMALL) / 2,
                        buf, COL_LABEL);
    }
}

// Build one of the two 128×128 pattern-table textures in grayscale.
// Pattern data is palette-agnostic (the runtime palette is decided per
// instance), so we render the four 2-bit pixel values as fixed shades.
static void rebuild_chr_table(int table_idx) {
    if (!chr_tex[table_idx]) return;
    uint16_t base = (uint16_t)(table_idx * 0x1000);
    static const uint32_t SHADE[4] = {
        0xFF1A1A22,  // pixel 0 (transparent in real rendering)
        0xFF6B6B7A,
        0xFFB2B2C0,
        0xFFE8E8F0
    };
    for (int tile = 0; tile < 256; tile++) {
        int tx = (tile % 16) * 8;
        int ty = (tile / 16) * 8;
        uint16_t addr = base + (uint16_t)(tile * 16);
        for (int row = 0; row < 8; row++) {
            uint8_t lo = ppu_bus_read_debug(addr + (uint16_t)row);
            uint8_t hi = ppu_bus_read_debug(addr + (uint16_t)row + 8);
            for (int col = 0; col < 8; col++) {
                uint8_t bit = (uint8_t)(7 - col);
                uint8_t pix = (uint8_t)((((hi >> bit) & 1) << 1)
                                       | ((lo >> bit) & 1));
                chr_pixels[table_idx][(ty + row) * 128 + (tx + col)]
                    = SHADE[pix];
            }
        }
    }
    SDL_UpdateTexture(chr_tex[table_idx], NULL, chr_pixels[table_idx],
                      128 * (int)sizeof(uint32_t));
}

// CHR pattern tables + the 32-byte palette RAM, drawn to the right of the
// sprite grid. CHR is invalidated by sampling 32 bytes spread across the
// 8 KiB and comparing against last frame (catches both mapper bank-switches
// and CHR-RAM writes without hashing the full table). Palette is cheap to
// re-fill every frame so we don't bother caching.
static void render_chr_and_palette(int x, int y) {
    // ---- CHR change detection
    bool chr_dirty = !chr_cache_inited;
    for (int i = 0; i < 32; i++) {
        uint8_t b = ppu_bus_read_debug((uint16_t)(i * 256));
        if (b != chr_sample[i]) {
            chr_sample[i] = b;
            chr_dirty = true;
        }
    }
    if (chr_dirty) {
        rebuild_chr_table(0);
        rebuild_chr_table(1);
        chr_cache_inited = true;
    }

    // ---- Header + CHR labels (small font)
    draw_text_small(x, y, "CHR $0000      CHR $1000", COL_LABEL);
    y += LINE_H_SMALL;

    if (chr_tex[0]) {
        SDL_Rect d0 = { x,       y, 128, 128 };
        SDL_RenderCopy(dbg_renderer, chr_tex[0], NULL, &d0);
    }
    if (chr_tex[1]) {
        SDL_Rect d1 = { x + 128, y, 128, 128 };
        SDL_RenderCopy(dbg_renderer, chr_tex[1], NULL, &d1);
    }
    // Thin separator between the two pattern tables
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, x + 128, y, x + 128, y + 128);
    y += 128 + 4;

    // ---- Palette swatches
    draw_text_small(x, y, "Palette  (BG row / SP row)", COL_LABEL);
    y += LINE_H_SMALL;

    const uint8_t *pal = ppu_palette_view();
    const int SW = 16;          // swatch width / height

    // 16 BG palette entries on one row, 16 sprite-palette on the next.
    for (int half = 0; half < 2; half++) {
        for (int i = 0; i < 16; i++) {
            uint8_t entry = pal[half * 0x10 + i];
            uint32_t argb = ppu_master_color(entry);
            SDL_SetRenderDrawColor(dbg_renderer,
                                   (argb >> 16) & 0xFF,
                                   (argb >>  8) & 0xFF,
                                   (argb      ) & 0xFF,
                                   255);
            SDL_Rect sw = { x + i * SW, y, SW, SW };
            SDL_RenderFillRect(dbg_renderer, &sw);
        }
        // Thin vertical separators every 4 swatches (palette groups)
        SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
        for (int g = 1; g < 4; g++) {
            SDL_RenderDrawLine(dbg_renderer, x + g * 4 * SW, y,
                               x + g * 4 * SW, y + SW);
        }
        y += SW;
    }
}

static void render_ppu_panel(void) {
    int y = PPU_Y + 4;
    char buf[LOG_LINE_LEN];

    // Heading + frame counters
    snprintf(buf, sizeof(buf), "--- PPU ---  frame=%u  sl=%d  dot=%d  %s",
             ppu_dbg.frame, ppu_dbg.scanline, ppu_dbg.dot,
             ppu_dbg.in_vblank ? "VBL" : "");
    draw_text(PAD_X, y, buf, COL_HEADING);
    y += LINE_H;

    // Loopy registers
    snprintf(buf, sizeof(buf), "v=$%04X  t=$%04X  x=%u  w=%u",
             ppu_dbg.v, ppu_dbg.t, ppu_dbg.x, ppu_dbg.w);
    draw_text(PAD_X, y, buf, COL_VALUE);
    y += LINE_H;

    // Memory-mapped registers
    snprintf(buf, sizeof(buf), "CTRL=$%02X  MASK=$%02X  STAT=$%02X  OAMa=$%02X",
             ppu_dbg.ctrl, ppu_dbg.mask, ppu_dbg.status, ppu_dbg.oam_addr);
    draw_text(PAD_X, y, buf, COL_VALUE);
    y += LINE_H;

    // Status flags + NMI count
    {
        SDL_Color sp0_col = ppu_dbg.sprite0_hit     ? COL_ON : COL_DIM;
        SDL_Color ovf_col = ppu_dbg.sprite_overflow ? COL_ON : COL_DIM;
        snprintf(buf, sizeof(buf), "NMI=%u   sp0_hit=%s   sp_ovf=%s",
                 ppu_dbg.nmi_count,
                 ppu_dbg.sprite0_hit ? "Y" : "N",
                 ppu_dbg.sprite_overflow ? "Y" : "N");
        draw_text(PAD_X, y, buf, COL_LABEL);
        // Small coloured dots to visualize the two flags at the line tail
        SDL_SetRenderDrawColor(dbg_renderer, sp0_col.r, sp0_col.g, sp0_col.b, 255);
        SDL_Rect d1 = { DBG_WIN_W - PAD_X - 28, y + 5, 6, 6 };
        SDL_RenderFillRect(dbg_renderer, &d1);
        SDL_SetRenderDrawColor(dbg_renderer, ovf_col.r, ovf_col.g, ovf_col.b, 255);
        SDL_Rect d2 = { DBG_WIN_W - PAD_X - 12, y + 5, 6, 6 };
        SDL_RenderFillRect(dbg_renderer, &d2);
    }
    y += LINE_H + 6;

    // Sprite-viewer heading
    {
        const uint8_t *oam0 = ppu_oam_view();
        int active = 0;
        for (int i = 0; i < 64; i++) {
            if (oam0[i * 4] < 240) active++;
        }
        snprintf(buf, sizeof(buf),
                 "Sprites  active=%d/64  sp0 @ (%u,%u)  %s",
                 active, oam0[3], oam0[0],
                 (ppu_dbg.ctrl & 0x20) ? "[8x16: top tile only]" : "[8x8]");
        draw_text(PAD_X, y, buf, COL_HEADING);
        y += LINE_H + 2;
    }

    // Visual sprite grid (left) + CHR pattern tables and palette (right)
    int grid_x = PAD_X + SPR_LABEL_W;
    render_sprite_grid(grid_x, y);
    render_chr_and_palette(grid_x + SPR_GRID_W + 10, y);
}

static void render_state(const CPU *cpu) {
    int y = STATE_Y;
    char buf[LOG_LINE_LEN];

    // ── CPU ──────────────────────────────────────────
    draw_text(PAD_X, y, "CPU", COL_HEADING);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "PC=$%04X  A=$%02X  X=$%02X  Y=$%02X  SP=$%02X",
             cpu->pc, cpu->a, cpu->x, cpu->y, cpu->sp);
    draw_text(PAD_X, y, buf, COL_VALUE);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "P=$%02X [%c%c%c%c%c%c%c%c]  CYC=%llu",
             cpu->status,
             cpu->status & FLAG_N ? 'N' : '-',
             cpu->status & FLAG_V ? 'V' : '-',
             cpu->status & FLAG_U ? 'U' : '-',
             cpu->status & FLAG_B ? 'B' : '-',
             cpu->status & FLAG_D ? 'D' : '-',
             cpu->status & FLAG_I ? 'I' : '-',
             cpu->status & FLAG_Z ? 'Z' : '-',
             cpu->status & FLAG_C ? 'C' : '-',
             (unsigned long long)cpu->total_cycles);
    draw_text(PAD_X, y, buf, COL_VALUE);
    y += LINE_H + 6;

    // ── APU ──────────────────────────────────────────
    draw_text(PAD_X, y, "APU", COL_HEADING);
    snprintf(buf, sizeof(buf), "$4015=$%02X   NMIs=%u   writes=%u",
             apu_dbg.last_status_value, ppu_dbg.nmi_count, apu_dbg.apu_write_count);
    draw_text(PAD_X + 40, y, buf, COL_LABEL);
    y += LINE_H;

    // Real-time rate — thresholds adapt to PAL (50 Hz) or NTSC (60 Hz)
    if (apu_dbg.measured_frame_hz > 0.0f) {
        float target = (cartridge && cartridge->is_pal) ? 50.0f : 60.0f;
        SDL_Color frm_col = (apu_dbg.measured_frame_hz > target + 2.0f
                          || apu_dbg.measured_frame_hz < target - 2.0f)
                          ? COL_OFF : COL_ON;
        snprintf(buf, sizeof(buf), "%.1f frm/s  %.1f NMI/s  %.0f smp/s  %s",
                 apu_dbg.measured_frame_hz, apu_dbg.measured_nmi_hz,
                 apu_dbg.measured_sample_hz,
                 (cartridge && cartridge->is_pal) ? "PAL" : "NTSC");
        draw_text(PAD_X, y, buf, frm_col);
    }
    y += LINE_H + 4;

    // ── Channels ─────────────────────────────────────
    {
        uint8_t vol = pulse1.constant_vol ? pulse1.envelope_vol : pulse1.envelope_decay;
        bool on = apu.pulse1_enabled && pulse1.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "P1 %s  vol=%2d  len=%3d  tmr=%4d  duty=%d",
                 on ? "ON " : "OFF", vol, pulse1.length_counter,
                 pulse1.timer_period, pulse1.duty);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        y += LINE_H;
        draw_bar(PAD_X, y, BAR_FULL_W, BAR_SLIM_H, vol / 15.0f, on ? COL_ON : COL_DIM);
        y += BAR_SLIM_H + 4;
    }
    {
        uint8_t vol = pulse2.constant_vol ? pulse2.envelope_vol : pulse2.envelope_decay;
        bool on = apu.pulse2_enabled && pulse2.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "P2 %s  vol=%2d  len=%3d  tmr=%4d  duty=%d",
                 on ? "ON " : "OFF", vol, pulse2.length_counter,
                 pulse2.timer_period, pulse2.duty);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        y += LINE_H;
        draw_bar(PAD_X, y, BAR_FULL_W, BAR_SLIM_H, vol / 15.0f, on ? COL_ON : COL_DIM);
        y += BAR_SLIM_H + 4;
    }
    {
        bool on = apu.triangle_enabled && triangle.length_counter > 0
                  && triangle.linear_counter > 0;
        float pct = on ? TRIANGLE_TABLE[triangle.seq_pos] / 15.0f : 0.0f;
        snprintf(buf, sizeof(buf), "TR %s  len=%3d  lin=%3d  tmr=%4d  seq=%2d",
                 on ? "ON " : "OFF", triangle.length_counter,
                 triangle.linear_counter, triangle.timer_period, triangle.seq_pos);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        y += LINE_H;
        SDL_Color tri_col = {100, 200, 255, 255};
        draw_bar(PAD_X, y, BAR_FULL_W, BAR_SLIM_H, pct, on ? tri_col : COL_DIM);
        y += BAR_SLIM_H + 4;
    }
    {
        uint8_t vol = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
        bool on = apu.noise_enabled && noise.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "NO %s  vol=%2d  len=%3d  tmr=%4d  mode=%d",
                 on ? "ON " : "OFF", vol, noise.length_counter,
                 noise.timer_period, noise.mode);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        y += LINE_H;
        SDL_Color noi_col = {255, 180, 80, 255};
        draw_bar(PAD_X, y, BAR_FULL_W, BAR_SLIM_H, vol / 15.0f, on ? noi_col : COL_DIM);
        y += BAR_SLIM_H + 4 + 6;
    }

    // ── Audio ────────────────────────────────────────
    draw_text(PAD_X, y, "Audio", COL_HEADING);
    if (apu_dbg.sample_rate > 0) {
        snprintf(buf, sizeof(buf), "@ %d Hz", apu_dbg.sample_rate);
        draw_text(PAD_X + 52, y, buf, COL_LABEL);
    }
    y += LINE_H;

    uint32_t avail = ring_buffer_available();
    snprintf(buf, sizeof(buf), "Buf %u/%d (%.0f%%)   peak=%.4f   nonzero=%u/%u",
             avail, RING_BUFFER_SIZE,
             100.0f * (float)avail / (float)RING_BUFFER_SIZE,
             apu_dbg.peak_sample,
             apu_dbg.nonzero_samples, apu_dbg.total_samples);
    draw_text(PAD_X, y, buf, COL_LABEL);
    y += LINE_H;
    SDL_Color buf_col = {80, 180, 255, 255};
    draw_bar(PAD_X, y, BAR_FULL_W, BAR_SLIM_H,
             (float)avail / (float)RING_BUFFER_SIZE, buf_col);
    y += BAR_SLIM_H + 4 + 6;

    // ── Controller ───────────────────────────────────
    {
        uint8_t cs = controller_state[0];
        static const char *btn_labels[] = {"A","B","Sel","Sta","Up","Dn","Lt","Rt"};
        static const uint8_t btn_bits[] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
        int spacing = (DBG_WIN_W - PAD_X * 2) / 8;
        int bx = PAD_X;
        for (int i = 0; i < 8; i++) {
            draw_text(bx, y, btn_labels[i], (cs & btn_bits[i]) ? COL_ON : COL_DIM);
            bx += spacing;
        }
        y += LINE_H + 6;
    }

    // ── Cartridge ────────────────────────────────────
    if (cartridge) {
        snprintf(buf, sizeof(buf), "PRG=%uKB  CHR=%uKB  Mapper=%u  %s",
                 cartridge->prg_size / 1024, cartridge->chr_size / 1024,
                 cartridge->mapper_id, cartridge->is_pal ? "PAL" : "NTSC");
        draw_text(PAD_X, y, buf, COL_VALUE);
    } else {
        draw_text(PAD_X, y, "(no cartridge)", COL_DIM);
    }
    y += LINE_H + 6;

    // ── Separator ────────────────────────────────────
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, PAD_X, y, DBG_WIN_W - PAD_X, y);
    y += 4;

    // ── Footer ───────────────────────────────────────
    if (show_paused) {
        draw_text(PAD_X, y, "|| PAUSED  (Q=step)", COL_OFF);
    } else {
        draw_text(PAD_X, y, "> RUNNING", COL_ON);
    }
    y += LINE_H;
    draw_text(PAD_X, y, "D=debug   R=pause   Q=step   ESC=quit", COL_DIM);
}

void debug_window_update(const CPU *cpu) {
    if (!dbg_visible || !dbg_renderer || !cpu) return;

    uint32_t now = SDL_GetTicks();
    if (now - last_draw_tick < DRAW_INTERVAL_MS) return;
    last_draw_tick = now;

    SDL_SetRenderDrawColor(dbg_renderer, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(dbg_renderer);

    render_log();
    render_separator();        // lines below log AND below PPU panel

    render_ppu_panel();        // PPU state + OAM table

    render_waveforms();

    // Separator between waveforms and state panel
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, PAD_X, STATE_Y - 2, DBG_WIN_W - PAD_X, STATE_Y - 2);

    render_state(cpu);

    SDL_RenderPresent(dbg_renderer);
}


