#include "timing.h"

#include <SDL_timer.h>

void timing_init(Timing *t, TimingMode mode) {
    t->mode        = mode;
    t->last_ticks  = SDL_GetTicks64();
    t->accumulated = 0.0;
}

uint32_t timing_update(Timing *t) {
    if (t->mode == UNBOUND) {
        // No throttle — caller runs one cpu_step per main loop iteration.
        // Returning UINT32_MAX signals "run one step and come back".
        return UINT32_MAX;
    }

    if (t->mode == AUDIO_SYNC) {
        // Run one frame's worth of CPU cycles per iteration (~29780 NTSC).
        // The ring buffer's blocking push is the real throttle — it stalls
        // the emulator when audio output can't keep up, locking us to the
        // exact sample rate of the audio hardware.
        return (uint32_t)CPU_HZ / 60;
    }

    uint64_t now = SDL_GetTicks64();
    uint64_t elapsed = now - t->last_ticks;
    t->last_ticks = now;

    if (elapsed > 50) elapsed = 50;

    t->accumulated += (elapsed / 1000.0) * CPU_HZ;

    uint32_t cycles = (uint32_t)t->accumulated;
    t->accumulated -= cycles;   // keep the fractional remainder
    return cycles;
}
