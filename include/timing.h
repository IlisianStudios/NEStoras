#pragma once
#ifndef NESTORAS_TIMING_H
#define NESTORAS_TIMING_H

#endif //NESTORAS_TIMING_H

#include <stdint.h>

#define CPU_HZ 1789773.0

typedef enum  {
    UNBOUND,
    FIXED,
    AUDIO_SYNC    // ring-buffer back-pressure is the sole throttle
} TimingMode;

typedef struct {
    TimingMode mode;
    uint64_t last_ticks;
    double accumulated;
} Timing;

void     timing_init(Timing *t, TimingMode mode);
uint32_t timing_update(Timing *t);  // returns cycles to run this frame
