#include "debug_window.h"
#include "apu.h"
#include "cpu.h"
#include "bus.h"
#include "ringbuffer.h"
#include "cartridge.h"

#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ---------- layout constants --------------------------------------------

#define DBG_WIN_W    360
#define DBG_WIN_H    680
#define FONT_SIZE    18
#define LINE_H       19
#define PAD_X        12
#define PAD_Y        10

// Top half: scrolling log
#define LOG_AREA_H   (DBG_WIN_H / 2)
#define LOG_MAX_LINES 128
#define LOG_LINE_LEN  96
#define LOG_VISIBLE   ((LOG_AREA_H - PAD_Y) / LINE_H - 1)  // -1 for heading

// Bottom half: live state
#define STATE_Y       (LOG_AREA_H + 6)

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

// ---------- text log ring buffer ----------------------------------------

static char  log_lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int   log_head  = 0;   // next write index
static int   log_count = 0;   // total stored (capped at LOG_MAX_LINES)

// ---------- window state ------------------------------------------------

static SDL_Window   *main_win     = NULL;
static SDL_Window   *dbg_win      = NULL;
static SDL_Renderer *dbg_renderer = NULL;
static TTF_Font     *dbg_font     = NULL;
static Uint32        dbg_win_id   = 0;
static bool          dbg_visible  = false;
static bool         *testing_mode_ptr = NULL;  // points to cpu.testing_mode

static uint32_t      last_draw_tick = 0;
static const uint32_t DRAW_INTERVAL_MS = 33;  // ~30 fps

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

// ---------- public API --------------------------------------------------

void debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_lines[log_head], LOG_LINE_LEN, fmt, args);
    va_end(args);
    log_head = (log_head + 1) % LOG_MAX_LINES;
    if (log_count < LOG_MAX_LINES) log_count++;
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

    dbg_win = SDL_CreateWindow(
        "NEStoras Debug",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        DBG_WIN_W, DBG_WIN_H,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!dbg_win) {
        fprintf(stderr, "debug_window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_CloseFont(dbg_font); dbg_font = NULL;
        return false;
    }
    dbg_win_id = SDL_GetWindowID(dbg_win);

    dbg_renderer = SDL_CreateRenderer(dbg_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!dbg_renderer) {
        fprintf(stderr, "debug_window: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(dbg_win); dbg_win = NULL;
        TTF_CloseFont(dbg_font);   dbg_font = NULL;
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

// ---------- rendering: top half (scrolling log) -------------------------

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

// ---------- rendering: separator ----------------------------------------

static void render_separator(void) {
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, PAD_X, LOG_AREA_H, DBG_WIN_W - PAD_X, LOG_AREA_H);
}

// ---------- rendering: bottom half (live state) -------------------------

static void render_state(const CPU *cpu) {
    int y = STATE_Y;
    char buf[LOG_LINE_LEN];

    // ── CPU
    draw_text(PAD_X, y, "--- CPU ---", COL_HEADING);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "PC=$%04X A=$%02X X=$%02X Y=$%02X SP=$%02X",
             cpu->pc, cpu->a, cpu->x, cpu->y, cpu->sp);
    draw_text(PAD_X, y, buf, COL_VALUE);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "P=$%02X [%c%c%c%c%c%c%c%c] CYC=%llu",
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

    // ── APU
    draw_text(PAD_X, y, "--- APU ---", COL_HEADING);
    y += LINE_H;

    snprintf(buf, sizeof(buf), "$4015=$%02X  NMIs=%u  wr=%u",
             apu_dbg.last_status_value, apu_dbg.nmi_count, apu_dbg.apu_write_count);
    draw_text(PAD_X, y, buf, COL_LABEL);
    y += LINE_H + 4;

    // ── Channels (compact: one line each + volume bar)
    {
        uint8_t vol = pulse1.constant_vol ? pulse1.envelope_vol : pulse1.envelope_decay;
        bool on = apu.pulse1_enabled && pulse1.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "P1 %s v=%2d l=%3d t=%4d d=%d",
                 on ? "ON " : "OFF", vol, pulse1.length_counter,
                 pulse1.timer_period, pulse1.duty);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        draw_bar(DBG_WIN_W - PAD_X - 60, y + 1, 50, LINE_H - 2, vol / 15.0f, COL_ON);
        y += LINE_H;
    }
    {
        uint8_t vol = pulse2.constant_vol ? pulse2.envelope_vol : pulse2.envelope_decay;
        bool on = apu.pulse2_enabled && pulse2.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "P2 %s v=%2d l=%3d t=%4d d=%d",
                 on ? "ON " : "OFF", vol, pulse2.length_counter,
                 pulse2.timer_period, pulse2.duty);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        draw_bar(DBG_WIN_W - PAD_X - 60, y + 1, 50, LINE_H - 2, vol / 15.0f, COL_ON);
        y += LINE_H;
    }
    {
        bool on = apu.triangle_enabled && triangle.length_counter > 0
                  && triangle.linear_counter > 0;
        snprintf(buf, sizeof(buf), "TR %s l=%3d lin=%3d t=%4d",
                 on ? "ON " : "OFF", triangle.length_counter,
                 triangle.linear_counter, triangle.timer_period);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        float pct = on ? TRIANGLE_TABLE[triangle.seq_pos] / 15.0f : 0.0f;
        draw_bar(DBG_WIN_W - PAD_X - 60, y + 1, 50, LINE_H - 2,
                 pct, (SDL_Color){100, 200, 255, 255});
        y += LINE_H;
    }
    {
        uint8_t vol = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
        bool on = apu.noise_enabled && noise.length_counter > 0 && vol > 0;
        snprintf(buf, sizeof(buf), "NO %s v=%2d l=%3d t=%4d m=%d",
                 on ? "ON " : "OFF", vol, noise.length_counter,
                 noise.timer_period, noise.mode);
        draw_text(PAD_X, y, buf, on ? COL_ON : COL_OFF);
        draw_bar(DBG_WIN_W - PAD_X - 60, y + 1, 50, LINE_H - 2,
                 vol / 15.0f, (SDL_Color){255, 180, 80, 255});
        y += LINE_H + 4;
    }

    // ── Audio
    draw_text(PAD_X, y, "--- Audio ---", COL_HEADING);
    y += LINE_H;

    uint32_t avail = ring_buffer_available();
    snprintf(buf, sizeof(buf), "Ring %u/%d (%.0f%%)  peak=%.4f",
             avail, RING_BUFFER_SIZE,
             100.0f * (float)avail / RING_BUFFER_SIZE, apu_dbg.peak_sample);
    draw_text(PAD_X, y, buf, COL_LABEL);
    draw_bar(DBG_WIN_W - PAD_X - 60, y + 1, 50, LINE_H - 2,
             (float)avail / RING_BUFFER_SIZE, (SDL_Color){80, 180, 255, 255});
    y += LINE_H + 4;

    // ── Controller
    {
        uint8_t cs = controller_state[0];
        static const char *btn[] = {"A","B","Sel","Sta","Up","Dn","Lt","Rt"};
        static const uint8_t bit[] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};
        int bx = PAD_X;
        for (int i = 0; i < 8; i++) {
            draw_text(bx, y, btn[i], (cs & bit[i]) ? COL_ON : COL_DIM);
            bx += 40;
        }
        y += LINE_H + 4;
    }

    // ── Cartridge
    if (cartridge) {
        snprintf(buf, sizeof(buf), "%uKB/%uKB  M%u  %s",
                 cartridge->prg_size / 1024, cartridge->chr_size / 1024,
                 cartridge->mapper_id, cartridge->is_pal ? "PAL" : "NTSC");
        draw_text(PAD_X, y, buf, COL_VALUE);
    } else {
        draw_text(PAD_X, y, "(no cartridge)", COL_DIM);
    }

    // ── Footer
    draw_text(PAD_X, DBG_WIN_H - LINE_H - PAD_Y, "D=toggle debug", COL_DIM);
}

// ---------- main update -------------------------------------------------

void debug_window_update(const CPU *cpu) {
    if (!dbg_visible || !dbg_renderer || !cpu) return;

    uint32_t now = SDL_GetTicks();
    if (now - last_draw_tick < DRAW_INTERVAL_MS) return;
    last_draw_tick = now;

    SDL_SetRenderDrawColor(dbg_renderer, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(dbg_renderer);

    render_log();
    render_separator();
    render_state(cpu);

    SDL_RenderPresent(dbg_renderer);
}


