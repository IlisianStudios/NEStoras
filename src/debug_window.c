#include "debug_window.h"
#include "apu.h"
#include "cpu.h"
#include "bus.h"
#include "ringbuffer.h"
#include "cartridge.h"

#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

// ---------- constants ---------------------------------------------------

#define DBG_WIN_W   360
#define DBG_WIN_H   680
#define FONT_SIZE   10
#define LINE_H      13        // vertical spacing per line
#define PAD_X       12
#define PAD_Y       10
#define MAX_LINES   64
#define MAX_LINE_LEN 128

// Colour palette
static const SDL_Color COL_BG      = {  24,  24,  32, 255 };
static const SDL_Color COL_HEADING = { 100, 200, 255, 255 };
static const SDL_Color COL_LABEL   = { 180, 180, 180, 255 };
static const SDL_Color COL_VALUE   = { 255, 255, 200, 255 };
static const SDL_Color COL_ON      = {  80, 255, 100, 255 };
static const SDL_Color COL_OFF     = { 255,  70,  70, 255 };
static const SDL_Color COL_DIM     = { 100, 100, 100, 255 };

// ---------- state -------------------------------------------------------

static SDL_Window   *dbg_win      = NULL;
static SDL_Renderer *dbg_renderer = NULL;
static TTF_Font     *dbg_font     = NULL;
static Uint32        dbg_win_id   = 0;
static bool          dbg_visible  = false;

// Throttle redraws — no need to redraw at 1000 fps
static uint32_t      last_draw_tick = 0;
static const uint32_t DRAW_INTERVAL_MS = 33;  // ~30 fps

// ---------- font search -------------------------------------------------

// Try several known monospaced fonts on macOS / Linux / Windows
static const char *font_candidates[] = {
    "/System/Library/Fonts/SFNSMono.ttf",            // macOS SF Mono
    "/System/Library/Fonts/Menlo.ttc",                // macOS Menlo
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",  // Linux
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",              // Arch
    "C:\\Windows\\Fonts\\consola.ttf",                       // Windows
    NULL
};

static TTF_Font *open_mono_font(int size) {
    for (int i = 0; font_candidates[i]; i++) {
        TTF_Font *f = TTF_OpenFont(font_candidates[i], size);
        if (f) return f;
    }
    return NULL;
}

// ---------- helpers -----------------------------------------------------

static void draw_text(int x, int y, const char *text, SDL_Color col) {
    if (!text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderText_Blended(dbg_font, text, col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(dbg_renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(dbg_renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void draw_bar(int x, int y, int w, int h, float pct, SDL_Color col) {
    // background
    SDL_SetRenderDrawColor(dbg_renderer, 40, 40, 50, 255);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(dbg_renderer, &bg);
    // filled portion
    int fw = (int)(pct * w);
    if (fw > w) fw = w;
    SDL_SetRenderDrawColor(dbg_renderer, col.r, col.g, col.b, col.a);
    SDL_Rect fg = { x, y, fw, h };
    SDL_RenderFillRect(dbg_renderer, &fg);
}

// ---------- public API --------------------------------------------------

bool debug_window_init(void) {
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

    // Window starts hidden — user presses D to show
    dbg_win = SDL_CreateWindow(
        "NEStoras — Debug",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        DBG_WIN_W, DBG_WIN_H,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!dbg_win) {
        fprintf(stderr, "debug_window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_CloseFont(dbg_font);
        dbg_font = NULL;
        return false;
    }
    dbg_win_id = SDL_GetWindowID(dbg_win);

    dbg_renderer = SDL_CreateRenderer(dbg_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!dbg_renderer) {
        fprintf(stderr, "debug_window: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(dbg_win);
        dbg_win = NULL;
        TTF_CloseFont(dbg_font);
        dbg_font = NULL;
        return false;
    }

    printf("Debug window ready (press D to toggle)\n");
    return true;
}

void debug_window_destroy(void) {
    if (dbg_renderer) { SDL_DestroyRenderer(dbg_renderer); dbg_renderer = NULL; }
    if (dbg_win)      { SDL_DestroyWindow(dbg_win);        dbg_win = NULL; }
    if (dbg_font)     { TTF_CloseFont(dbg_font);           dbg_font = NULL; }
    TTF_Quit();
}

void debug_window_toggle(void) {
    if (!dbg_win) return;
    dbg_visible = !dbg_visible;
    if (dbg_visible)
        SDL_ShowWindow(dbg_win);
    else
        SDL_HideWindow(dbg_win);
}

bool debug_window_visible(void) {
    return dbg_visible;
}

bool debug_window_handle_event(const SDL_Event *event) {
    if (!dbg_win) return false;

    // Handle the debug window's own close button
    if (event->type == SDL_WINDOWEVENT &&
        event->window.windowID == dbg_win_id &&
        event->window.event == SDL_WINDOWEVENT_CLOSE) {
        dbg_visible = false;
        SDL_HideWindow(dbg_win);
        return true;
    }
    return false;
}

void debug_window_update(const CPU *cpu) {
    if (!dbg_visible || !dbg_renderer || !cpu) return;

    // Throttle redraws
    uint32_t now = SDL_GetTicks();
    if (now - last_draw_tick < DRAW_INTERVAL_MS) return;
    last_draw_tick = now;

    // Clear
    SDL_SetRenderDrawColor(dbg_renderer, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(dbg_renderer);

    int y = PAD_Y;
    char buf[MAX_LINE_LEN];

    // ── CPU ─────────────────────────────────────────────
    draw_text(PAD_X, y, "--- CPU ---", COL_HEADING);
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
    y += LINE_H + 4;

    // ── APU Status ──────────────────────────────────────
    draw_text(PAD_X, y, "--- APU ---", COL_HEADING);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "$4015=$%02X  NMIs=%u  APU writes=%u",
             apu_dbg.last_status_value, apu_dbg.nmi_count, apu_dbg.apu_write_count);
    draw_text(PAD_X, y, buf, COL_LABEL);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "Frame mode=%d  IRQ inhibit=%d  Frame IRQ=%d",
             apu.frame_mode, apu.irq_inhibit, apu.frame_irq);
    draw_text(PAD_X, y, buf, COL_LABEL);
    y += LINE_H + 4;

    // ── Channels ────────────────────────────────────────

    // Pulse 1
    {
        uint8_t vol = pulse1.constant_vol ? pulse1.envelope_vol : pulse1.envelope_decay;
        bool on = apu.pulse1_enabled && pulse1.length_counter > 0 && vol > 0;

        draw_text(PAD_X, y, "Pulse 1", on ? COL_ON : COL_OFF);
        snprintf(buf, sizeof(buf), "en=%d", apu.pulse1_enabled);
        draw_text(PAD_X + 90, y, buf, apu.pulse1_enabled ? COL_ON : COL_OFF);
        y += LINE_H;

        snprintf(buf, sizeof(buf), "vol=%2d  len=%3d  tmr=%4d  duty=%d  seq=%d",
                 vol, pulse1.length_counter, pulse1.timer_period, pulse1.duty, pulse1.seq_pos);
        draw_text(PAD_X + 8, y, buf, COL_VALUE);

        // Volume bar
        draw_bar(DBG_WIN_W - PAD_X - 100, y + 2, 90, LINE_H - 4,
                 vol / 15.0f, COL_ON);
        y += LINE_H + 2;
    }

    // Pulse 2
    {
        uint8_t vol = pulse2.constant_vol ? pulse2.envelope_vol : pulse2.envelope_decay;
        bool on = apu.pulse2_enabled && pulse2.length_counter > 0 && vol > 0;

        draw_text(PAD_X, y, "Pulse 2", on ? COL_ON : COL_OFF);
        snprintf(buf, sizeof(buf), "en=%d", apu.pulse2_enabled);
        draw_text(PAD_X + 90, y, buf, apu.pulse2_enabled ? COL_ON : COL_OFF);
        y += LINE_H;

        snprintf(buf, sizeof(buf), "vol=%2d  len=%3d  tmr=%4d  duty=%d  seq=%d",
                 vol, pulse2.length_counter, pulse2.timer_period, pulse2.duty, pulse2.seq_pos);
        draw_text(PAD_X + 8, y, buf, COL_VALUE);

        draw_bar(DBG_WIN_W - PAD_X - 100, y + 2, 90, LINE_H - 4,
                 vol / 15.0f, COL_ON);
        y += LINE_H + 2;
    }

    // Triangle
    {
        bool on = apu.triangle_enabled && triangle.length_counter > 0 && triangle.linear_counter > 0;

        draw_text(PAD_X, y, "Triangle", on ? COL_ON : COL_OFF);
        snprintf(buf, sizeof(buf), "en=%d", apu.triangle_enabled);
        draw_text(PAD_X + 90, y, buf, apu.triangle_enabled ? COL_ON : COL_OFF);
        y += LINE_H;

        snprintf(buf, sizeof(buf), "len=%3d  lin=%3d  tmr=%4d  seq=%2d",
                 triangle.length_counter, triangle.linear_counter,
                 triangle.timer_period, triangle.seq_pos);
        draw_text(PAD_X + 8, y, buf, COL_VALUE);

        float tri_pct = on ? TRIANGLE_TABLE[triangle.seq_pos] / 15.0f : 0.0f;
        draw_bar(DBG_WIN_W - PAD_X - 100, y + 2, 90, LINE_H - 4,
                 tri_pct, (SDL_Color){100, 200, 255, 255});
        y += LINE_H + 2;
    }

    // Noise
    {
        uint8_t vol = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
        bool on = apu.noise_enabled && noise.length_counter > 0 && vol > 0;

        draw_text(PAD_X, y, "Noise", on ? COL_ON : COL_OFF);
        snprintf(buf, sizeof(buf), "en=%d  mode=%d", apu.noise_enabled, noise.mode);
        draw_text(PAD_X + 90, y, buf, apu.noise_enabled ? COL_ON : COL_OFF);
        y += LINE_H;

        snprintf(buf, sizeof(buf), "vol=%2d  len=%3d  tmr=%4d  lfsr=$%04X",
                 vol, noise.length_counter, noise.timer_period, noise.lfsr);
        draw_text(PAD_X + 8, y, buf, COL_VALUE);

        draw_bar(DBG_WIN_W - PAD_X - 100, y + 2, 90, LINE_H - 4,
                 vol / 15.0f, (SDL_Color){255, 180, 80, 255});
        y += LINE_H + 6;
    }

    // ── Audio Pipeline ──────────────────────────────────
    draw_text(PAD_X, y, "--- Audio ---", COL_HEADING);
    y += LINE_H;

    uint32_t avail = ring_buffer_available();
    snprintf(buf, sizeof(buf), "Ring: %u / %d  (%.0f%%)",
             avail, RING_BUFFER_SIZE, 100.0f * avail / RING_BUFFER_SIZE);
    draw_text(PAD_X, y, buf, COL_LABEL);
    draw_bar(PAD_X + 280, y + 2, 150, LINE_H - 4,
             (float)avail / RING_BUFFER_SIZE, (SDL_Color){80, 180, 255, 255});
    y += LINE_H;

    snprintf(buf, sizeof(buf), "Samples: %u/%u nonzero  peak=%.4f",
             apu_dbg.nonzero_samples, apu_dbg.total_samples, apu_dbg.peak_sample);
    draw_text(PAD_X, y, buf, COL_LABEL);
    y += LINE_H + 6;

    // ── Controller ──────────────────────────────────────
    draw_text(PAD_X, y, "--- Controller ---", COL_HEADING);
    y += LINE_H;

    {
        uint8_t cs = controller_state[0];
        static const char *btn_names[] = {"A","B","Sel","Sta","Up","Dn","Lt","Rt"};
        static const uint8_t btn_bits[] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};

        int bx = PAD_X;
        for (int i = 0; i < 8; i++) {
            bool pressed = cs & btn_bits[i];
            draw_text(bx, y, btn_names[i], pressed ? COL_ON : COL_DIM);
            bx += 48;
        }
        y += LINE_H + 6;
    }

    // ── Cartridge Info ──────────────────────────────────
    draw_text(PAD_X, y, "--- Cartridge ---", COL_HEADING);
    y += LINE_H;

    if (cartridge) {
        snprintf(buf, sizeof(buf), "PRG=%uKB  CHR=%uKB  Mapper=%u  %s",
                 cartridge->prg_size / 1024, cartridge->chr_size / 1024,
                 cartridge->mapper_id, cartridge->is_pal ? "PAL" : "NTSC");
        draw_text(PAD_X, y, buf, COL_VALUE);
    } else {
        draw_text(PAD_X, y, "(no cartridge loaded)", COL_DIM);
    }
    y += LINE_H;

    // ── Footer ──────────────────────────────────────────
    y = DBG_WIN_H - LINE_H - PAD_Y;
    draw_text(PAD_X, y, "D=toggle  ESC=close emulator", COL_DIM);

    SDL_RenderPresent(dbg_renderer);
}


