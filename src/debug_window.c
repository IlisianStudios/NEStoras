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

#define DBG_WIN_W    520
#define DBG_WIN_H    960
#define FONT_SIZE    14
#define LINE_H       15
#define PAD_X        12
#define PAD_Y        10

#define LOG_MAX_LINES 128
#define LOG_LINE_LEN  96

// Volume bars — drawn on their own slim row below the channel text
#define BAR_FULL_W    (DBG_WIN_W - PAD_X * 2)
#define BAR_SLIM_H    4

// State section height — must match render_state() exactly:
//   CPU(3 lines +6)  APU(1 +4)
//   3 channels(line+bar+gap4)  last channel(+6 extra)
//   Audio hdr(1)  Audio data(1)  Audio bar(+gap4+6)
//   Controller(1+6)  Cartridge(1+6)  Separator(4)
//   Footer(2 lines + PAD_Y)
#define STATE_H  ((3*LINE_H+6) + (LINE_H+4) \
                 + 3*(LINE_H+BAR_SLIM_H+4) + (LINE_H+BAR_SLIM_H+4+6) \
                 + LINE_H + LINE_H + (BAR_SLIM_H+4+6) \
                 + (LINE_H+6) + (LINE_H+6) + 4 \
                 + LINE_H + LINE_H + PAD_Y)
#define STATE_Y         (DBG_WIN_H - STATE_H)

// Waveform section — 5 graphs between log and state
#define WAVE_COUNT      5
#define WAVE_H          24
#define WAVE_GAP        3
#define WAVE_LABEL_W    20
#define WAVE_GRAPH_W    (BAR_FULL_W - WAVE_LABEL_W)
#define WAVE_SECTION_H  (LINE_H + 4 + WAVE_COUNT * (WAVE_H + WAVE_GAP))
#define WAVE_Y          (STATE_Y - WAVE_SECTION_H - 4)

// Log area sits above the waveform section
#define LOG_AREA_H      (WAVE_Y - 2)
#define LOG_VISIBLE     ((LOG_AREA_H - PAD_Y) / LINE_H - 1)

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
static Uint32        dbg_win_id   = 0;
static bool          dbg_visible  = false;
static bool         *testing_mode_ptr = NULL;  // points to cpu.testing_mode

static uint32_t      last_draw_tick = 0;
static const uint32_t DRAW_INTERVAL_MS = 16;   // ~60 fps
static uint32_t      last_wave_tick = 0;
static const uint32_t WAVE_UPDATE_MS = 400;
static bool          show_paused = false;

// Waveform snapshots (copied from apu_dbg every 400 ms)
static float wave_snap[WAVE_COUNT][APU_WAVE_LEN];

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

static void snapshot_waveforms(void) {
    int pos = apu_dbg.wave_pos;
    for (int i = 0; i < APU_WAVE_LEN; i++) {
        int s = (pos + i) % APU_WAVE_LEN;
        wave_snap[0][i] = apu_dbg.wave_p1[s];
        wave_snap[1][i] = apu_dbg.wave_p2[s];
        wave_snap[2][i] = apu_dbg.wave_tri[s];
        wave_snap[3][i] = apu_dbg.wave_noi[s];
        wave_snap[4][i] = apu_dbg.wave_mix[s];
    }
}

static void draw_waveform(int x, int y, int w, int h,
                          const float *samples, SDL_Color col) {
    // Background + midline
    SDL_SetRenderDrawColor(dbg_renderer, 28, 28, 40, 255);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(dbg_renderer, &bg);
    SDL_SetRenderDrawColor(dbg_renderer, 55, 55, 75, 255);
    SDL_RenderDrawLine(dbg_renderer, x, y + h / 2, x + w - 1, y + h / 2);

    // Connected line graph
    SDL_SetRenderDrawColor(dbg_renderer, col.r, col.g, col.b, col.a);
    int px0 = x, py0 = y + h / 2;
    for (int i = 0; i < APU_WAVE_LEN; i++) {
        float sv = samples[i];
        if (sv > 1.0f) sv = 1.0f;
        if (sv < 0.0f) sv = 0.0f;
        int px = x + (int)((float)i / (float)(APU_WAVE_LEN - 1) * (float)(w - 1));
        int py = y + h - 1 - (int)(sv * (float)(h - 1));
        if (i > 0)
            SDL_RenderDrawLine(dbg_renderer, px0, py0, px, py);
        px0 = px; py0 = py;
    }
}

static void render_waveforms(void) {
    static const char *labels[]  = { "P1", "P2", "TR", "NO", "MX" };
    static const SDL_Color cols[] = {
        { 80, 255, 100, 255 },   // P1: green
        { 100, 200, 255, 255 },  // P2: cyan
        { 255, 200, 80,  255 },  // TR: amber
        { 255, 100, 200, 255 },  // NO: pink
        { 220, 220, 220, 255 },  // MX: white
    };

    draw_text(PAD_X, WAVE_Y, "Waveforms", COL_HEADING);

    int y = WAVE_Y + LINE_H + 4;
    for (int ch = 0; ch < WAVE_COUNT; ch++) {
        draw_text(PAD_X, y + (WAVE_H - LINE_H) / 2, labels[ch], cols[ch]);
        draw_waveform(PAD_X + WAVE_LABEL_W, y, WAVE_GRAPH_W, WAVE_H,
                      wave_snap[ch], cols[ch]);
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
             apu_dbg.last_status_value, apu_dbg.nmi_count, apu_dbg.apu_write_count);
    draw_text(PAD_X + 40, y, buf, COL_LABEL);
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

    // Snapshot waveforms at 400 ms intervals
    if (now - last_wave_tick >= WAVE_UPDATE_MS) {
        last_wave_tick = now;
        snapshot_waveforms();
    }

    SDL_SetRenderDrawColor(dbg_renderer, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(dbg_renderer);

    render_log();
    render_separator();        // line between log and waveforms

    render_waveforms();

    // Separator between waveforms and state panel
    SDL_SetRenderDrawColor(dbg_renderer, COL_SEP.r, COL_SEP.g, COL_SEP.b, 255);
    SDL_RenderDrawLine(dbg_renderer, PAD_X, STATE_Y - 2, DBG_WIN_W - PAD_X, STATE_Y - 2);

    render_state(cpu);

    SDL_RenderPresent(dbg_renderer);
}


