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

    uint64_t now = SDL_GetTicks64();
    uint64_t elapsed = now - t->last_ticks;
    t->last_ticks = now;

    if (elapsed > 50) elapsed = 50;

    t->accumulated += (elapsed / 1000.0) * CPU_HZ;

    uint32_t cycles = (uint32_t)t->accumulated;
    t->accumulated -= cycles;   // keep the fractional remainder
    return cycles;
}
