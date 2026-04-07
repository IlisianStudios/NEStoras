#include "apu.h"

#include "cpu.h"
#include "cartridge.h"
#include "ringbuffer.h"
#include "debug_window.h"

APU apu;

Pulse pulse1;
Pulse pulse2;
Triangle triangle;
Noise noise;
DMC dmc;

static double sample_accumulator = 0.0;
static double cycles_per_sample = 1789773.0 / 44100.0;
static int    output_sample_rate = 44100;
static bool otherCycle = false;

static uint32_t nmi_cycles = 0;
static uint32_t nmi_period = 29780;  // NTSC default; PAL = 33248

// NTSC APU frame counter periods (CPU cycles)
static const uint32_t FRAME_PERIOD_4_NTSC[4] = { 7457, 14913, 22371, 29829 };
static const uint32_t FRAME_PERIOD_5_NTSC[5] = { 7457, 14913, 22371, 29829, 37281 };

// PAL APU frame counter periods (CPU cycles)
static const uint32_t FRAME_PERIOD_4_PAL[4] = { 8313, 16627, 24939, 33253 };
static const uint32_t FRAME_PERIOD_5_PAL[5] = { 8313, 16627, 24939, 33253, 41565 };

// Pointers set by apu_init based on region
static const uint32_t *frame_periods_4 = FRAME_PERIOD_4_NTSC;
static const uint32_t *frame_periods_5 = FRAME_PERIOD_5_NTSC;

// DMC rate tables
static const uint16_t dmc_ntsc_rates[16] = {428, 380, 340, 320, 286, 254, 226, 214,                                                                                                                                                               190, 160, 142, 128, 106,  84,  72,  54};
static const uint16_t dmc_pal_rates[16]  = {398,354,316,298,276,236,210,198,176,148,132,118,98,78,66,50};
static const uint16_t *dmc_rates;

// PPU-timing thresholds for fake $2002 (set by apu_init)
uint32_t ppu_vblank_end    = 2387;   // fc < this → vblank still active after NMI
uint32_t ppu_vblank_start  = 27393;  // fc >= this → entering vblank
uint32_t ppu_sp0_hit_start = 5000;   // fc >= this → sprite 0 hit
uint32_t ppu_sp0_hit_end   = 29000;  // fc < this → sprite 0 hit

APUDebug apu_dbg;

uint32_t apu_get_frame_cycles(void) {
    return nmi_cycles;
}

void apu_set_output_sample_rate(int rate) {
    output_sample_rate = rate;
    bool pal = (cartridge && cartridge->is_pal);
    double cpu_freq = pal ? 1662607.0 : 1789773.0;
    cycles_per_sample = cpu_freq / rate;
    apu_dbg.sample_rate = rate;
}

void apu_init(void) {
    memset(&pulse1,   0, sizeof(pulse1));
    memset(&pulse2,   0, sizeof(pulse2));
    memset(&triangle, 0, sizeof(triangle));
    memset(&noise,    0, sizeof(noise));
    memset(&dmc,      0, sizeof(dmc));
    memset(&apu,      0, sizeof(apu));
    noise.lfsr         = 1;
    dmc.bits_remaining = 8;    // shift register starts empty but well-defined
    dmc.silence        = true;
    nmi_cycles         = 0;
    sample_accumulator = 0.0;
    otherCycle         = false;
    ppu_nmi_enable     = false;

    // Region-specific timing
    bool pal = (cartridge && cartridge->is_pal);
    if (pal) {
        dmc_rates = dmc_pal_rates;
        nmi_period        = 33248;
        cycles_per_sample = 1662607.0 / output_sample_rate;
        frame_periods_4   = FRAME_PERIOD_4_PAL;
        frame_periods_5   = FRAME_PERIOD_5_PAL;
        // Fake PPU thresholds scaled for PAL frame
        ppu_vblank_end    = 2665;   // ~2387 * 33248/29780
        ppu_vblank_start  = 30583;  // ~27393 * 33248/29780
        ppu_sp0_hit_start = 5582;   // ~5000 * 33248/29780
        ppu_sp0_hit_end   = 32379;  // ~29000 * 33248/29780
    } else {
        dmc_rates = dmc_ntsc_rates;
        nmi_period        = 29780;
        cycles_per_sample = 1789773.0 / output_sample_rate;
        frame_periods_4   = FRAME_PERIOD_4_NTSC;
        frame_periods_5   = FRAME_PERIOD_5_NTSC;
        ppu_vblank_end    = 2387;
        ppu_vblank_start  = 27393;
        ppu_sp0_hit_start = 5000;
        ppu_sp0_hit_end   = 29000;
    }
}

void apu_debug_reset(void) {
    memset(&apu_dbg, 0, sizeof(apu_dbg));
    apu_dbg.diag_interval  = 300;
    apu_dbg.sample_rate    = output_sample_rate;  // preserve across ROM reloads
}

void apu_debug_print(CPU *cpu) {
    if (!apu_dbg.enabled) return;

    apu_dbg.diag_counter++;
    if (apu_dbg.diag_counter < apu_dbg.diag_interval) return;
    apu_dbg.diag_counter = 0;

    uint8_t p1v = pulse1.constant_vol ? pulse1.envelope_vol : pulse1.envelope_decay;
    uint8_t p2v = pulse2.constant_vol ? pulse2.envelope_vol : pulse2.envelope_decay;
    uint8_t nv  = noise.constant_vol  ? noise.envelope_vol  : noise.envelope_decay;

    debug_log("[APU] %u/%u nonzero peak=%.4f ring=%u/%d",
           apu_dbg.nonzero_samples, apu_dbg.total_samples, apu_dbg.peak_sample,
           ring_buffer_available(), RING_BUFFER_SIZE);

    debug_log("  $4015=$%02X wr=%u en: p1=%d p2=%d tri=%d noi=%d",
           apu_dbg.last_status_value, apu_dbg.status_write_count,
           apu.pulse1_enabled, apu.pulse2_enabled,
           apu.triangle_enabled, apu.noise_enabled);

    debug_log("  p1:v=%d l=%d t=%d | p2:v=%d l=%d t=%d",
           p1v, pulse1.length_counter, pulse1.timer_period,
           p2v, pulse2.length_counter, pulse2.timer_period);

    debug_log("  tri:l=%d lin=%d | noi:v=%d l=%d lfsr=$%04X",
           triangle.length_counter, triangle.linear_counter,
           nv, noise.length_counter, noise.lfsr);

    debug_log("  NMIs=%u wr=%u PC=$%04X cyc=%llu",
           apu_dbg.nmi_count, apu_dbg.apu_write_count,
           cpu->pc, (unsigned long long)cpu->total_cycles);
}

void quarter_frame_pulse(Pulse *pulse) {
    if (pulse->envelope_start) {
        pulse->envelope_start = false;
        pulse->envelope_decay = 15;
        pulse->envelope_divider = pulse->envelope_vol;
    }
    else {
        if (pulse->envelope_divider > 0) {
            pulse->envelope_divider--;
        }
        else {
            pulse->envelope_divider = pulse->envelope_vol;
            if (pulse->envelope_decay > 0) {
                pulse->envelope_decay--;
            }
            else if (pulse->length_halt) {
                pulse->envelope_decay = 15;
            }
        }
    }

    pulse->volume = pulse->constant_vol ? pulse->envelope_vol : pulse->envelope_decay;
}

void half_frame_pulse(Pulse *pulse) {
    if (!pulse->length_halt && pulse->length_counter > 0)
        pulse->length_counter--;

    if (pulse->sweep_divider > 0)
        pulse->sweep_divider--;
    else {
        pulse->sweep_divider = pulse->sweep_period;
        if (pulse->sweep_enabled && pulse->sweep_shift > 0) {
            const uint16_t delta = pulse->timer_period >> pulse->sweep_shift;

            if (pulse->sweep_negate) {
                pulse->timer_period -= delta;
                // In 2A03, the negate mode adds an extra -1 for pulse 1
                if (pulse == &pulse1)
                    pulse->timer_period -= 1;
            }
            else {
                pulse->timer_period += delta;
            }
        }

    }

    if (pulse->sweep_reload){
        pulse->sweep_reload = false;
        pulse->sweep_divider = pulse->sweep_period;
    }
}

static void clock_envelope(Noise *n) {
    if (n->envelope_start) {
        n->envelope_start   = false;
        n->envelope_decay   = 15;
        n->envelope_divider = n->envelope_vol;
    } else {
        if (n->envelope_divider > 0) {
            n->envelope_divider--;
        } else {
            n->envelope_divider = n->envelope_vol;
            if (n->envelope_decay > 0)
                n->envelope_decay--;
            else if (n->length_halt)
                n->envelope_decay = 15;
        }
    }
    n->volume = n->constant_vol ? n->envelope_vol : n->envelope_decay;
}

void clock_quarter_frame(void) {
    quarter_frame_pulse(&pulse1);
    quarter_frame_pulse(&pulse2);
    clock_envelope(&noise);

    // Triangle linear counter
    if (triangle.linear_reload_flag)
        triangle.linear_counter = triangle.linear_reload;
    else if (triangle.linear_counter > 0)
        triangle.linear_counter--;
    if (!triangle.length_halt)
        triangle.linear_reload_flag = false;
}

void clock_half_frame(void) {
    half_frame_pulse(&pulse1);
    half_frame_pulse(&pulse2);

    // Triangle length counter
    if (!triangle.length_halt && triangle.length_counter > 0)
        triangle.length_counter--;

    // Noise length counter
    if (!noise.length_halt && noise.length_counter > 0)
        noise.length_counter--;
}

static uint8_t pulse_output(Pulse *p, bool enabled) {
    if (!enabled) return 0;
    if (p->length_counter == 0) return 0;
    if (p->timer_period < 8) return 0;
    // Sweep target period overflow mute: always computed, regardless of sweep enable.
    // Negate never overflows; only the addition case can exceed $7FF.
    if (!p->sweep_negate) {
        uint16_t target = p->timer_period + (p->timer_period >> p->sweep_shift);
        if (target > 0x7FF) return 0;
    }
    if (!DUTY_TABLE[p->duty][p->seq_pos]) return 0;
    uint8_t vol = p->constant_vol ? p->envelope_vol : p->envelope_decay;
    return vol;
}

// ---------- DMC memory reader -------------------------------------------

static void dmc_fetch_byte(CPU *cpu) {
    if (dmc.sample_buffer_full || dmc.bytes_remaining == 0) return;

    dmc.sample_buffer      = bus_read(dmc.current_address);
    dmc.sample_buffer_full = true;

    // Address wraps $8000–$FFFF
    if (dmc.current_address == 0xFFFF)
        dmc.current_address = 0x8000;
    else
        dmc.current_address++;

    dmc.bytes_remaining--;
    if (dmc.bytes_remaining == 0) {
        if (dmc.loop) {
            dmc.current_address = dmc.sample_address;
            dmc.bytes_remaining = dmc.sample_length;
        } else if (dmc.irq_enable) {
            apu.dmc_irq = true;
            cpu_irq(cpu);
        }
    }
}

static void clock_pulse_timer(Pulse *p) {
    if (p->timer_current == 0) {
        p->timer_current = p->timer_period;
        p->seq_pos = (p->seq_pos + 1) & 7;
    } else {
        p->timer_current--;
    }
}

void apu_step(CPU *cpu) {
    // Fake NMI — PPU vblank fires every frame (NTSC: ~29780, PAL: ~33248 CPU cycles)
    if (++nmi_cycles >= nmi_period) {
        nmi_cycles = 0;
        if (ppu_nmi_enable) {
            cpu_nmi(cpu);
            apu_dbg.nmi_count++;
        }
    }

    // The APU runs at half the CPU clock.
    // Pulse and noise timers tick every other CPU cycle.
    // Triangle ticks every CPU cycle.
    otherCycle = !otherCycle;

    if (otherCycle) {
        clock_pulse_timer(&pulse1);
        clock_pulse_timer(&pulse2);

        // Noise timer + LFSR (also clocked at APU rate)
        if (noise.timer_current == 0) {
            noise.timer_current = noise.timer_period;
            uint16_t feedback = (noise.lfsr & 1) ^
                                ((noise.mode ? (noise.lfsr >> 6) : (noise.lfsr >> 1)) & 1);
            noise.lfsr = (noise.lfsr >> 1) | (feedback << 14);
        } else {
            noise.timer_current--;
        }

        // DMC timer + output unit (clocked at APU rate)
        if (apu.dmc_enabled || dmc.bits_remaining > 0) {
            if (dmc.timer_current == 0) {
                dmc.timer_current = dmc.timer_period;

                // Clock output unit: adjust level by one bit
                if (!dmc.silence) {
                    if (dmc.shift_register & 1) {
                        if (dmc.output_level <= 125) dmc.output_level += 2;
                    } else {
                        if (dmc.output_level >= 2)  dmc.output_level -= 2;
                    }
                }
                dmc.shift_register >>= 1;
                dmc.bits_remaining--;

                if (dmc.bits_remaining == 0) {
                    dmc.bits_remaining = 8;
                    if (dmc.sample_buffer_full) {
                        dmc.shift_register     = dmc.sample_buffer;
                        dmc.sample_buffer_full = false;
                        dmc.silence            = false;
                        dmc_fetch_byte(cpu);   // immediately try to fill buffer
                    } else {
                        dmc.silence = true;
                    }
                }
            } else {
                dmc.timer_current--;
            }
        }
    }

    // Triangle timer (clocked every CPU cycle)
    if (triangle.timer_current == 0) {
        triangle.timer_current = triangle.timer_period;
        if (triangle.length_counter > 0 && triangle.linear_counter > 0)
            triangle.seq_pos = (triangle.seq_pos + 1) & 31;
    } else {
        triangle.timer_current--;
    }

    apu.frame_cycles++;

    const uint32_t *periods = (apu.frame_mode == 0) ? frame_periods_4 : frame_periods_5;
    uint8_t steps = (apu.frame_mode == 0) ? 4 : 5;

    for (uint8_t i = 0; i < steps; i++) {
        if (apu.frame_cycles == periods[i]) {

            // Mode 1, step 4 (i==3): nothing happens, counter keeps running
            if (apu.frame_mode == 1 && i == 3)
                break;

            clock_quarter_frame();  // envelopes + triangle linear counter

            // Half frame: length counters + sweep units
            // Mode 0: fires at steps 2 and 4 (i==1, i==3)
            // Mode 1: fires at steps 2 and 5 (i==1, i==4)
            bool is_half = (i == 1) ||
                           (apu.frame_mode == 0 && i == 3) ||
                           (apu.frame_mode == 1 && i == 4);

            if (is_half) {
                clock_half_frame();
            }

            // IRQ: only in 4-step mode, only at final step, only if not inhibited
            if (apu.frame_mode == 0 && i == 3 && !apu.irq_inhibit) {
                apu.frame_irq = true;
            }

            if (i == steps - 1) {
                apu.frame_cycles = 0;
            }

            if (apu.frame_irq) {
                cpu_irq(cpu);
            }

            break;
        }
    }

    sample_accumulator += 1.0;
    if (sample_accumulator >= cycles_per_sample) {
        sample_accumulator -= cycles_per_sample;
        float mix = apu_mix();
        apu_dbg.total_samples++;
        if (mix > 0.001f || mix < -0.001f) apu_dbg.nonzero_samples++;
        float absmix = mix < 0 ? -mix : mix;
        if (absmix > apu_dbg.peak_sample) apu_dbg.peak_sample = absmix;
        ring_buffer_push(mix);

        if (!apu_dbg.enabled) return;

        int wp = apu_dbg.wave_pos;
        apu_dbg.wave_p1[wp]  = (float)pulse_output(&pulse1, apu.pulse1_enabled) / 15.0f;
        apu_dbg.wave_p2[wp]  = (float)pulse_output(&pulse2, apu.pulse2_enabled) / 15.0f;
        apu_dbg.wave_tri[wp] = (apu.triangle_enabled
                                && triangle.length_counter > 0
                                && triangle.linear_counter > 0)
                               ? TRIANGLE_TABLE[triangle.seq_pos] / 15.0f : 0.0f;
        uint8_t nvol = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
        apu_dbg.wave_noi[wp] = (apu.noise_enabled
                                && noise.length_counter > 0
                                && (noise.lfsr & 1) == 0)
                               ? nvol / 15.0f : 0.0f;
        apu_dbg.wave_dmc[wp] = apu.dmc_enabled ? (float)dmc.output_level / 127.0f : 0.0f;
        apu_dbg.wave_mix[wp] = mix * 0.5f;   // mix is 2× boosted; scale back to [0,1]
        apu_dbg.wave_pos = (wp + 1) % APU_WAVE_LEN;
    }
}

float apu_mix(void) {
    uint8_t p1 = pulse_output(&pulse1, apu.pulse1_enabled);
    uint8_t p2 = pulse_output(&pulse2, apu.pulse2_enabled);

    float pulse_out = 0.0f;
    if (p1 + p2 > 0)
        pulse_out = 95.88f / (8128.0f / (float)(p1 + p2) + 100.0f);

    uint8_t tri = 0;
    if (apu.triangle_enabled && triangle.length_counter > 0 && triangle.linear_counter > 0)
        tri = TRIANGLE_TABLE[triangle.seq_pos];

    uint8_t noi = 0;
    if (apu.noise_enabled && noise.length_counter > 0 && (noise.lfsr & 1) == 0) {
        // Compute noise volume on-the-fly (same fix as pulse)
        noi = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
    }

    float tnd_out = 0.0f;
    float tnd_sum = (float)tri / 8227.0f
                  + (float)noi / 12241.0f
                  + (float)dmc.output_level / 22638.0f;
    if (tnd_sum > 0.0f)
        tnd_out = 159.79f / (1.0f / tnd_sum + 100.0f);

    return (pulse_out + tnd_out) * 2.0f;
}

uint8_t apu_read(uint16_t addr) {
    if (addr != 0x4015) return 0;
    uint8_t status = 0;
    if (pulse1.length_counter > 0)   status |= 0x01;
    if (pulse2.length_counter > 0)   status |= 0x02;
    if (triangle.length_counter > 0) status |= 0x04;
    if (noise.length_counter > 0)    status |= 0x08;
    if (dmc.bytes_remaining > 0)     status |= 0x10;
    if (apu.frame_irq)               status |= 0x40;
    if (apu.dmc_irq)                 status |= 0x80;
    apu.frame_irq = false;           // reading $4015 clears the frame IRQ flag
    // DMC IRQ is NOT cleared by reading; cleared by writing $4015 or disabling in $4010
    return status;
}

void apu_write(uint16_t addr, uint8_t data) {
    apu_dbg.apu_write_count++;
    switch (addr) {
        case 0x4017: {
            apu.frame_mode = (data >> 7) & 1;
            apu.irq_inhibit = (data >> 6) & 1;

            if (apu.irq_inhibit)
                apu.frame_irq = false;

            // Exact hardware behavior: the reset takes effect 3-4 cycles later
            apu.frame_cycles = 0;

            if (apu.frame_mode == 1) {
                clock_half_frame();   // length counters + sweeps
                clock_quarter_frame(); // envelopes + linear counter
            }
            break;
        }
        case 0x4015:
            apu_dbg.status_write_count++;
            apu_dbg.last_status_value = data;
            apu.pulse1_enabled   = (data >> 0) & 1;
            apu.pulse2_enabled   = (data >> 1) & 1;
            apu.triangle_enabled = (data >> 2) & 1;
            apu.noise_enabled    = (data >> 3) & 1;
            if (!apu.pulse1_enabled)   pulse1.length_counter   = 0;
            if (!apu.pulse2_enabled)   pulse2.length_counter   = 0;
            if (!apu.triangle_enabled) triangle.length_counter = 0;
            if (!apu.noise_enabled)    noise.length_counter    = 0;
            // DMC: writing $4015 always clears the DMC IRQ flag
            apu.dmc_irq = false;
            if (data & 0x10) {
                apu.dmc_enabled = true;
                if (dmc.bytes_remaining == 0) {
                    dmc.current_address = dmc.sample_address;
                    dmc.bytes_remaining = dmc.sample_length;
                }
            } else {
                apu.dmc_enabled = false;
                dmc.bytes_remaining = 0;
            }
            break;
        case 0x4000:
            pulse1.duty = (data >> 6) & 0x03;
            pulse1.length_halt = (data >> 5) & 1;
            pulse1.constant_vol = (data >> 4) & 1;
            pulse1.envelope_vol = data & 0x0F;
            pulse1.volume = pulse1.constant_vol ? pulse1.envelope_vol : pulse1.envelope_decay;
            break;
        case 0x4001:
            pulse1.sweep_enabled = (data >> 7) & 1;
            pulse1.sweep_period = (data >> 4) & 0x07;
            pulse1.sweep_negate = (data >> 3) & 1;
            pulse1.sweep_shift = data & 0x07;
            pulse1.sweep_reload = true;
            break;
        case 0x4002:
            pulse1.timer_period = (pulse1.timer_period & 0x700) | data;
            break;
        case 0x4003:
            pulse1.timer_period = (pulse1.timer_period & 0x00FF) | ((data & 0x07) << 8);
            pulse1.length_counter = LENGTH_TABLE[(data >> 3) & 0x1F];
            pulse1.envelope_start = true;
            pulse1.seq_pos = 0;
            break;
        case 0x4004:
            pulse2.duty = (data >> 6) & 0x03;
            pulse2.length_halt = (data >> 5) & 1;
            pulse2.constant_vol = (data >> 4) & 1;
            pulse2.envelope_vol = data & 0x0F;
            pulse2.volume = pulse2.constant_vol ? pulse2.envelope_vol : pulse2.envelope_decay;
            break;
        case 0x4005:
            pulse2.sweep_enabled = (data >> 7) & 1;
            pulse2.sweep_period = (data >> 4) & 0x07;
            pulse2.sweep_negate = (data >> 3) & 1;
            pulse2.sweep_shift = data & 0x07;
            pulse2.sweep_reload = true;
            break;
        case 0x4006:
            pulse2.timer_period = (pulse2.timer_period & 0x700) | data;
            break;
        case 0x4007:
            pulse2.timer_period = (pulse2.timer_period & 0x00FF) | ((data & 0x07) << 8);
            pulse2.length_counter = LENGTH_TABLE[(data >> 3) & 0x1F];
            pulse2.envelope_start = true;
            pulse2.seq_pos = 0;
            break;
        case 0x4008:
            triangle.length_halt        = (data >> 7) & 1;
            triangle.linear_reload      = data & 0x7F;
            break;
        case 0x400A:
            triangle.timer_period = (triangle.timer_period & 0x700) | data;
            break;
        case 0x400B:
            triangle.timer_period       = (triangle.timer_period & 0x00FF) | ((data & 0x07) << 8);
            triangle.length_counter     = LENGTH_TABLE[(data >> 3) & 0x1F];
            triangle.linear_reload_flag = true;
            break;
        case 0x400C:
            noise.length_halt  = (data >> 5) & 1;
            noise.constant_vol = (data >> 4) & 1;
            noise.envelope_vol = data & 0x0F;
            noise.volume = noise.constant_vol ? noise.envelope_vol : noise.envelope_decay;
            break;
        case 0x400E:
            noise.mode         = (data >> 7) & 1;
            noise.timer_period = NOISE_PERIOD_TABLE[data & 0x0F];
            break;
        case 0x400F:
            noise.length_counter  = LENGTH_TABLE[(data >> 3) & 0x1F];
            noise.envelope_start  = true;
            break;
        case 0x4010:
            dmc.irq_enable = (data >> 7) & 1;
            if (!dmc.irq_enable) apu.dmc_irq = false;
            dmc.loop         = (data >> 6) & 1;
            dmc.timer_period = dmc_rates[data & 0x0F];
            break;
        case 0x4011:
            dmc.output_level = data & 0x7F;
            break;
        case 0x4012:
            dmc.sample_address = 0xC000 + (data << 6);
            break;
        case 0x4013:
            dmc.sample_length = (data << 4) | 1;
    }
}
