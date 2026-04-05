#include "apu.h"

#include "cpu.h"
#include "ringbuffer.h"

APU apu;

// NTSC periods (in CPU cycles):
static const uint32_t FRAME_PERIOD_4[4] = { 3728, 7456, 11185, 14914 };
static const uint32_t FRAME_PERIOD_5[5] = { 3728, 7456, 11185, 14914, 18640 };

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

void clock_quarter_frame(void) {
    quarter_frame_pulse(&pulse1);
    quarter_frame_pulse(&pulse2);

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
}

static uint8_t pulse_output(Pulse *p, bool enabled) {
    if (!enabled) return 0;
    if (p->length_counter == 0) return 0;
    if (p->timer_period < 8) return 0;
    if (!DUTY_TABLE[p->duty][p->seq_pos]) return 0;
    return p->volume;
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
    clock_pulse_timer(&pulse1);
    clock_pulse_timer(&pulse2);

    // Triangle timer
    if (triangle.timer_current == 0) {
        triangle.timer_current = triangle.timer_period;
        if (triangle.length_counter > 0 && triangle.linear_counter > 0)
            triangle.seq_pos = (triangle.seq_pos + 1) & 31;
    } else {
        triangle.timer_current--;
    }

    apu.frame_cycles++;

    const uint32_t *periods = (apu.frame_mode == 0) ? FRAME_PERIOD_4 : FRAME_PERIOD_5;
    uint8_t steps = (apu.frame_mode == 0) ? 4 : 5;

    for (uint8_t i = 0; i < steps; i++) {
        if (apu.frame_cycles == periods[i]) {

            bool is_half = (i == 1) || (i == 3);  // steps 2 and 4 (0-indexed 1 and 3)
            bool is_5step_extra = (apu.frame_mode == 1 && i == 4);

            clock_quarter_frame();  // always fires at every step

            if (is_half && !is_5step_extra) {
                clock_half_frame();
            }

            // IRQ: only in 4-step mode, only at final step, only if not inhibited
            if (apu.frame_mode == 0 && i == 3 && !apu.irq_inhibit) {
                apu.frame_irq = true;
            }

            // Reset counter after final step
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
        float sample = apu_mix();          // get current output
        ring_buffer_push(sample);          // push to audio thread
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

    float tnd_out = 0.0f;
    if (tri > 0)
        tnd_out = 159.79f / (1.0f / ((float)tri / 8227.0f) + 100.0f);

    return pulse_out + tnd_out;
}

uint8_t apu_read(uint16_t addr) {
    return 0;
}

void apu_write(uint16_t addr, uint8_t data) {
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
            // Enable/disable channels
            apu.pulse1_enabled    = (data >> 0) & 1;
            apu.pulse2_enabled    = (data >> 1) & 1;
            apu.triangle_enabled  = (data >> 2) & 1;
            apu.noise_enabled     = (data >> 3) & 1;
            apu.dmc_enabled       = (data >> 4) & 1;
            // Disabling a channel immediately zeros its length counter
            // (add when you implement channels)
            break;
        case 0x4000:
            pulse1.duty = (data >> 6) & 0x03;
            pulse1.length_halt = (data >> 5) & 1;
            pulse1.constant_vol = (data >> 4) & 1;
            pulse1.envelope_vol = data & 0x0F;
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
        // $400C-$400F: noise
        // add cases as you implement
    }
}
