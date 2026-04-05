#pragma once

#ifndef NESTORAS_APU_H
#define NESTORAS_APU_H
#include "cpu.h"

#endif //NESTORAS_APU_H
#include <stdint.h>
#include <stdbool.h>

static double sample_accumulator = 0.0;
typedef struct Pulse Pulse;
static const double cycles_per_sample = 1789773.0 / 44100.0;
static bool otherCycle = false;
static const uint8_t LENGTH_TABLE[32] = {
    10, 254, 20,  2, 40,  4, 80,  6,
   160,   8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22,
   192,  24, 72, 26, 16, 28, 32, 30,
};

struct Pulse{
    uint8_t  duty;           // 0-3, selects one of 4 duty cycle patterns
    uint8_t  seq_pos;        // 0-7, position in 8-step sequence
    uint16_t timer_period;   // 11-bit reload value
    uint16_t timer_current;  // countdown
    uint8_t  length_counter;
    bool     length_halt;    // also "envelope loop" flag
    bool     constant_vol;
    uint8_t  envelope_vol;   // 0-15
    uint8_t  envelope_decay; // current decay level
    bool     envelope_start; // flag: restart envelope
    // sweep unit fields
    uint16_t sweep_period;
    bool     sweep_enabled;
    bool     sweep_negate;
    uint8_t  sweep_shift;
    bool     sweep_reload;
    uint8_t  sweep_divider;
    uint8_t envelope_divider;
    uint8_t volume;
};

static const uint8_t DUTY_TABLE[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},  // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0},  // 25%
    {0, 1, 1, 1, 1, 0, 0, 0},  // 50%
    {1, 0, 0, 1, 1, 1, 1, 1},  // 25% negated
};

static const uint8_t TRIANGLE_TABLE[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
};

static  Pulse pulse1;
static  Pulse pulse2;

typedef struct {
    uint8_t  seq_pos;
    uint16_t timer_period;
    uint16_t timer_current;
    uint8_t  length_counter;
    bool     length_halt;        // also "linear counter control"
    uint8_t  linear_counter;
    uint8_t  linear_reload;      // reload value written to $4008
    bool     linear_reload_flag;
} Triangle;

static Triangle triangle;

typedef struct {
    // --- frame sequencer ---
    uint32_t frame_cycles;   // counts up, resets at period
    uint8_t  frame_mode;     // 0 = 4-step, 1 = 5-step
    bool     irq_inhibit;
    bool     frame_irq;      // pending IRQ flag

    // --- channel enable flags (from $4015) ---
    bool     pulse1_enabled;
    bool     pulse2_enabled;
    bool     triangle_enabled;
    bool     noise_enabled;
    bool     dmc_enabled;

    // --- pulse 1, pulse 2, triangle, noise structs go here ---
    // (add as you implement each one)

    // --- downsampler ---
    double   sample_accumulator;
} APU;

extern APU apu;

void apu_step(CPU *cpu);
float apu_mix(void);
uint8_t apu_read(uint16_t addr); // $4015 only
void apu_write(uint16_t addr, uint8_t data); // $4000-$4017