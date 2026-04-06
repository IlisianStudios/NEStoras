#pragma once

#ifndef NESTORAS_APU_H
#define NESTORAS_APU_H
#include "cpu.h"

#endif //NESTORAS_APU_H
#include <stdint.h>
#include <stdbool.h>

typedef struct Pulse Pulse;

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

extern Pulse pulse1;
extern Pulse pulse2;

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

extern Triangle triangle;

static const uint16_t NOISE_PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

typedef struct {
    uint8_t  length_counter;
    bool     length_halt;       // also envelope loop
    bool     constant_vol;
    uint8_t  envelope_vol;
    uint8_t  envelope_decay;
    bool     envelope_start;
    uint8_t  envelope_divider;
    uint8_t  volume;
    bool     mode;              // false = bit6 feedback, true = bit1 feedback
    uint16_t timer_period;
    uint16_t timer_current;
    uint16_t lfsr;              // 15-bit shift register, must init to 1
} Noise;

extern Noise noise;

typedef struct {
    bool     irq_enable;       // DMC interrupt enable
    bool     loop;
    uint16_t timer_period;   //
    uint16_t sample_address;   // sample address = 0xC000 + (addr * 64)
    uint16_t sample_length;    // sample length = (len * 16) + 1 bytes
    uint16_t current_address;
    uint16_t bytes_remaining;
    uint8_t  shift_register;
    uint8_t  bits_remaining;
    bool     silence;          // if true, output is silent regardless of shift register
    uint16_t direct_load;
    uint8_t sample_buffer;
} DMC;

extern DMC dmc;

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

    // --- downsampler ---
    double   sample_accumulator;
} APU;

extern APU apu;

void apu_init(void);
void apu_step(CPU *cpu);
float apu_mix(void);
uint8_t apu_read(uint16_t addr); // $4015 only
void apu_write(uint16_t addr, uint8_t data); // $4000-$4017
uint32_t apu_get_frame_cycles(void);

#define APU_WAVE_LEN 256

typedef struct {
    bool     enabled;            // set true when cpu.testing_mode is on
    uint32_t nmi_count;
    uint32_t apu_write_count;
    uint32_t status_write_count; // $4015 writes
    uint32_t nonzero_samples;
    uint32_t total_samples;
    uint8_t  last_status_value;  // last value written to $4015
    float    peak_sample;
    uint32_t ppu_write_count;    // $2000 NMI-enable toggling
    uint32_t diag_interval;      // frames between debug prints
    uint32_t diag_counter;       // counts up to diag_interval

    float wave_p1[APU_WAVE_LEN];
    float wave_p2[APU_WAVE_LEN];
    float wave_tri[APU_WAVE_LEN];
    float wave_noi[APU_WAVE_LEN];
    float wave_mix[APU_WAVE_LEN];
    int   wave_pos;              // next-write index (circular)
} APUDebug;

extern APUDebug apu_dbg;

void apu_debug_reset(void);
void apu_debug_print(CPU *cpu);

// PPU timing thresholds (set by apu_init based on PAL/NTSC)
extern uint32_t ppu_vblank_end;
extern uint32_t ppu_vblank_start;
extern uint32_t ppu_sp0_hit_start;
extern uint32_t ppu_sp0_hit_end;

