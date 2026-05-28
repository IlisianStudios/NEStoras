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
    bool     irq_enable;        // bit 7 of $4010
    bool     loop;              // bit 6 of $4010
    uint16_t timer_period;      // from rate table
    uint16_t timer_current;     // countdown
    uint16_t sample_address;    // $C000 + (reg * 64)
    uint16_t sample_length;     // (reg * 16) + 1 bytes
    uint16_t current_address;   // fetch address, wraps $8000–$FFFF
    uint16_t bytes_remaining;   // bytes left in current sample
    uint8_t  shift_register;    // 8-bit output shift register
    uint8_t  bits_remaining;    // bits left to shift (0 triggers reload)
    bool     silence;           // output unit is silent
    uint8_t  sample_buffer;     // byte fetched from memory, waiting to be shifted
    bool     sample_buffer_full;// is sample_buffer valid?
    uint8_t  output_level;      // 7-bit DAC output (0–127)
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
    bool     dmc_irq;           // DMC interrupt pending

    // --- downsampler ---
    double   sample_accumulator;
} APU;

extern APU apu;

void apu_init(void);
void apu_set_output_sample_rate(int rate); // call after SDL device is opened
void apu_step(CPU *cpu);
float apu_mix(void);
uint8_t apu_read(uint16_t addr); // $4015 only
void apu_write(uint16_t addr, uint8_t data); // $4000-$4017

// Scrolling scope: each column = APU_SCOPE_SPP audio samples (peak-held).
// At 44100 Hz / 256 SPP = ~172 cols/sec → ~2.9 cols/frame at 60fps → smooth scroll.
#define APU_SCOPE_W   512   // columns in the circular scope buffer (power of 2)
#define APU_SCOPE_SPP 256   // audio samples accumulated per column

typedef struct {
    bool     enabled;            // set true when cpu.testing_mode is on
    uint32_t apu_write_count;
    uint32_t status_write_count; // $4015 writes
    uint32_t nonzero_samples;
    uint32_t total_samples;
    uint8_t  last_status_value;  // last value written to $4015
    float    peak_sample;
    uint32_t ppu_write_count;    // $2000 NMI-enable toggling
    uint32_t diag_interval;      // frames between debug prints
    uint32_t diag_counter;       // counts up to diag_interval

    // Scrolling scope buffers — one entry per APU_SCOPE_SPP audio samples
    float scope_p1 [APU_SCOPE_W];
    float scope_p2 [APU_SCOPE_W];
    float scope_tri[APU_SCOPE_W];
    float scope_noi[APU_SCOPE_W];
    float scope_dmc[APU_SCOPE_W];
    float scope_mix[APU_SCOPE_W];
    int   scope_pos;             // next write column (circular, mod APU_SCOPE_W)
    int   scope_count;           // audio samples accumulated in current column
    float scope_acc[6];          // per-channel peak accumulators [p1,p2,tri,noi,dmc,mix]

    int   sample_rate;           // actual SDL device rate (set by apu_set_output_sample_rate)

    // Real-time rate measurement — frame_count / nmi_count come from ppu_dbg
    uint32_t rate_tick;          // SDL_GetTicks at last measurement
    uint32_t rate_nmi_snap;      // ppu_dbg.nmi_count at last measurement
    uint32_t rate_sample_snap;   // total_samples at last measurement
    uint32_t rate_frame_snap;    // ppu_dbg.frame_count at last measurement
    float    measured_nmi_hz;    // NMIs per real second
    float    measured_frame_hz;  // total frame periods per real second
    float    measured_sample_hz; // audio samples per real second
} APUDebug;

extern APUDebug apu_dbg;

void apu_debug_reset(void);
void apu_debug_print(CPU *cpu);

