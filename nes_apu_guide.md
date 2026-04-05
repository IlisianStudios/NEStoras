# NES APU: Complete Implementation Guide

## The mental model

The APU is a **tone generator chip** baked into the 2A03 CPU die. It doesn't "play" audio — it continuously generates analog signals from simple mathematical wave rules, then mixes them. Your job is to simulate that in software and push the result to your system's audio output.

The APU has **5 channels**. Each is an independent signal generator:

| Channel  | Wave type           | What it sounds like   |
|----------|---------------------|-----------------------|
| Pulse 1  | Square wave         | Melody, leads         |
| Pulse 2  | Square wave         | Harmony               |
| Triangle | Triangle wave       | Bass lines            |
| Noise    | Pseudo-random       | Drums, effects        |
| DMC      | Delta-modulated PCM | Sampled voices        |

You will almost certainly hear real game sound after implementing just Pulse 1 + Pulse 2 + Triangle. That's your feel-good milestone.

---

## Architecture diagram

```svg
<svg width="100%" viewBox="0 0 680 620" xmlns="http://www.w3.org/2000/svg">
<defs>
  <marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
    <path d="M2 1L8 5L2 9" fill="none" stroke="#888" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
  </marker>
</defs>

<!-- CPU -->
<rect x="240" y="20" width="200" height="40" rx="8" fill="#e8e6e1" stroke="#999" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="340" y="44" text-anchor="middle" fill="#2c2c2a">CPU (2A03 @ 1.789 MHz)</text>

<!-- Clock arrows down -->
<line x1="300" y1="60" x2="300" y2="88" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>
<text font-family="sans-serif" font-size="12" x="308" y="78" fill="#666">tick every cycle</text>
<line x1="380" y1="60" x2="380" y2="88" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>

<!-- Frame sequencer -->
<rect x="160" y="88" width="360" height="44" rx="8" fill="#cecbf6" stroke="#7f77dd" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="340" y="106" text-anchor="middle" fill="#26215c">Frame sequencer</text>
<text font-family="sans-serif" font-size="12" x="340" y="122" text-anchor="middle" fill="#534ab7">240 Hz clocks: envelope / length counter / sweep</text>

<!-- Frame seq clock lines to channels (dashed) -->
<line x1="180" y1="132" x2="100" y2="188" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3" marker-end="url(#arrow)"/>
<line x1="260" y1="132" x2="230" y2="188" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3" marker-end="url(#arrow)"/>
<line x1="340" y1="132" x2="340" y2="188" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3" marker-end="url(#arrow)"/>
<line x1="430" y1="132" x2="460" y2="188" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3" marker-end="url(#arrow)"/>

<!-- Pulse 1 -->
<rect x="30" y="188" width="140" height="56" rx="8" fill="#e1f5ee" stroke="#1d9e75" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="100" y="208" text-anchor="middle" fill="#04342c">Pulse 1</text>
<text font-family="sans-serif" font-size="12" x="100" y="228" text-anchor="middle" fill="#0f6e56">Timer · Seq · Env · Sweep</text>

<!-- Pulse 2 -->
<rect x="190" y="188" width="140" height="56" rx="8" fill="#e1f5ee" stroke="#1d9e75" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="260" y="208" text-anchor="middle" fill="#04342c">Pulse 2</text>
<text font-family="sans-serif" font-size="12" x="260" y="228" text-anchor="middle" fill="#0f6e56">Timer · Seq · Env · Sweep</text>

<!-- Triangle -->
<rect x="270" y="188" width="140" height="56" rx="8" fill="#e6f1fb" stroke="#378add" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="340" y="208" text-anchor="middle" fill="#042c53">Triangle</text>
<text font-family="sans-serif" font-size="12" x="340" y="228" text-anchor="middle" fill="#185fa5">Timer · Linear counter</text>

<!-- Noise -->
<rect x="390" y="188" width="140" height="56" rx="8" fill="#faece7" stroke="#d85a30" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="460" y="208" text-anchor="middle" fill="#4a1b0c">Noise</text>
<text font-family="sans-serif" font-size="12" x="460" y="228" text-anchor="middle" fill="#993c1d">Timer · LFSR · Envelope</text>

<!-- DMC -->
<rect x="510" y="188" width="140" height="56" rx="8" fill="#f1efe8" stroke="#888" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="580" y="208" text-anchor="middle" fill="#2c2c2a">DMC</text>
<text font-family="sans-serif" font-size="12" x="580" y="228" text-anchor="middle" fill="#5f5e5a">DMA · Delta decode</text>

<!-- implement last badge -->
<rect x="514" y="250" width="132" height="18" rx="4" fill="none" stroke="#aaa" stroke-width="0.5" stroke-dasharray="3 2"/>
<text font-family="sans-serif" font-size="12" x="580" y="262" text-anchor="middle" fill="#888">implement last</text>

<!-- Output lines to mixer -->
<line x1="100" y1="244" x2="100" y2="330" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>
<line x1="260" y1="244" x2="220" y2="330" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>
<line x1="340" y1="244" x2="320" y2="330" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>
<line x1="460" y1="244" x2="440" y2="330" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>
<line x1="580" y1="268" x2="520" y2="330" stroke="#888" stroke-width="0.5" stroke-dasharray="3 2" marker-end="url(#arrow)"/>

<!-- output labels -->
<text font-family="sans-serif" font-size="12" x="154" y="298" text-anchor="middle" fill="#999">0–15</text>
<text font-family="sans-serif" font-size="12" x="240" y="310" text-anchor="middle" fill="#999">0–15</text>
<text font-family="sans-serif" font-size="12" x="334" y="310" text-anchor="middle" fill="#999">0–15</text>
<text font-family="sans-serif" font-size="12" x="456" y="310" text-anchor="middle" fill="#999">0–15</text>
<text font-family="sans-serif" font-size="12" x="558" y="315" text-anchor="middle" fill="#999">0–127</text>

<!-- Non-linear Mixer -->
<rect x="80" y="330" width="460" height="52" rx="8" fill="#faeeda" stroke="#ba7517" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="310" y="350" text-anchor="middle" fill="#412402">Non-linear mixer (resistor ladder)</text>
<text font-family="sans-serif" font-size="11" x="310" y="368" text-anchor="middle" fill="#854f0b">pulse = 95.88/(8128/(p1+p2)+100)  ·  tnd = 159.79/(1/(tr/8227+no/12241+dmc/22638)+100)</text>

<!-- Mixer to downsampler -->
<line x1="310" y1="382" x2="310" y2="430" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>

<!-- Downsampler -->
<rect x="180" y="430" width="260" height="52" rx="8" fill="#cecbf6" stroke="#7f77dd" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="310" y="450" text-anchor="middle" fill="#26215c">Downsampler</text>
<text font-family="sans-serif" font-size="12" x="310" y="468" text-anchor="middle" fill="#534ab7">1 sample every ~40.58 CPU cycles → 44100 Hz</text>

<!-- Downsampler to ring buffer -->
<line x1="310" y1="482" x2="310" y2="526" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)"/>

<!-- Ring buffer -->
<rect x="180" y="526" width="260" height="52" rx="8" fill="#e8e6e1" stroke="#888" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="14" font-weight="500" x="310" y="546" text-anchor="middle" fill="#2c2c2a">Ring buffer (thread-safe)</text>
<text font-family="sans-serif" font-size="12" x="310" y="564" text-anchor="middle" fill="#5f5e5a">APU writes · audio callback reads</text>

<!-- IRQ line -->
<path d="M520 110 Q600 110 600 40 L440 40" fill="none" stroke="#e24b4a" stroke-width="0.5" stroke-dasharray="4 3" marker-end="url(#arrow)"/>
<text font-family="sans-serif" font-size="12" x="598" y="82" text-anchor="end" fill="#e24b4a">IRQ</text>

<!-- $4000-$4017 write bus -->
<path d="M440 40 Q560 40 560 100 Q560 200 510 210" fill="none" stroke="#888" stroke-width="0.5" marker-end="url(#arrow)" opacity="0.4"/>
<text font-family="sans-serif" font-size="12" x="572" y="155" text-anchor="start" fill="#999">$4000–$4017</text>

<!-- legend -->
<line x1="42" y1="500" x2="62" y2="500" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text font-family="sans-serif" font-size="12" x="68" y="504" fill="#666">frame sequencer clock</text>
<line x1="42" y1="520" x2="62" y2="520" stroke="#888" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="12" x="68" y="524" fill="#666">audio signal / data flow</text>
</svg>
```

---

## How the APU fits into the system

The CPU drives everything. The APU is clocked by the CPU: **every CPU cycle, you tick the APU**. You already have the CPU loop, so you'll add `apu_tick()` calls inside it, the same way you call `ppu_tick()` (even dummy ones now).

The APU also generates an **IRQ** signal that can interrupt the CPU. You'll need a wire for that — a flag the APU can set and the CPU checks.

---

## The clock hierarchy — understand this before writing a single line of code

Everything in the APU derives from clock division. The NTSC NES runs at **~1.789773 MHz** CPU clock.

```
CPU clock (1.789773 MHz)
  └─ APU frame sequencer: fires every 3728.5/7457 CPU cycles
       └─ Controls envelopes, sweeps, length counters (4 or 5 steps)
  └─ APU timer: per-channel countdown loaded from registers
       └─ Controls oscillator frequency (pitch)
```

The **timer** is what makes pitch happen. Each channel has a timer register. The timer counts down every APU cycle (every 2 CPU cycles for pulse/noise/DMC, every CPU cycle for triangle). When it hits zero, it reloads and **clocks the waveform sequencer** — the thing that steps through the wave shape.

The **frame sequencer** is what makes volume, vibrato, and note-cut happen. It runs at ~240 Hz and fires various "clocks" at the units that shape the sound envelope.

---

## The units inside each channel

Every channel (except DMC) is built from these re-used building blocks:

**Timer** — a countdown. Loaded from two registers (low 8 bits + high 3 bits = 11-bit period). When it expires, it clocks the sequencer. Period → pitch relationship: `freq = CPU_clock / (16 × (period + 1))`.

**Sequencer** — an index into a small lookup table. For pulse channels, it's an 8-entry table (the duty cycle waveform). For triangle, it's a 32-entry table (the actual triangle shape, as discrete steps). Each timer expiry advances this index.

**Length counter** — a countdown in "length table units". When loaded, it starts at a value from a hardcoded 32-entry table. Each frame sequencer clock decrements it. When it hits zero, the channel goes silent. This is how notes end.

**Envelope** — controls volume over time. Either a constant volume or a decaying envelope that ramps down from 15 to 0. The frame sequencer clocks it. If looping, it restarts at 15 after hitting 0.

**Sweep unit** (pulse channels only) — periodically adjusts the timer period up or down, causing pitch bends. Two's complement math with a hardware quirk (pulse 1 and pulse 2 use different negate behavior — this is a known hardware bug you must replicate or some games sound wrong).

---

## Step-by-step implementation plan

### Step 1: Audio output plumbing (do this first)

Before any APU logic, get silence coming out of your speakers. You need a callback-driven audio pipeline. On most platforms (SDL2, miniaudio, PortAudio), you register a callback that fires when the audio device needs more samples.

```c
// SDL2 example — your callback fills a buffer
void audio_callback(void *userdata, Uint8 *stream, int len) {
    // len bytes needed (len/2 samples for 16-bit mono)
    // pull from your ring buffer, fill stream
    int16_t *out = (int16_t *)stream;
    int num_samples = len / sizeof(int16_t);
    for (int i = 0; i < num_samples; i++) {
        out[i] = ring_buffer_pop();
    }
}
```

Set your audio output to **44100 Hz, 16-bit mono**. This is the sample rate of your final output. The APU runs at ~1.789 MHz internally — you will **downsample** to 44100 Hz.

The key insight: you don't output one sample per APU tick. You accumulate APU output continuously, then every `1789773 / 44100 ≈ 40.58 CPU cycles` you push one sample to your ring buffer. A simple approach is a fractional counter:

```c
static double sample_accumulator = 0.0;
static const double cycles_per_sample = 1789773.0 / 44100.0;

void apu_tick(void) {
    // ... run APU logic ...

    sample_accumulator += 1.0;
    if (sample_accumulator >= cycles_per_sample) {
        sample_accumulator -= cycles_per_sample;
        float sample = apu_mix();          // get current output
        ring_buffer_push(sample);          // push to audio thread
    }
}
```

Get this ring buffer working and playing zeros before touching channel logic.

---

### The ring buffer

The ring buffer is the only shared data structure between your **emulation thread** (which calls `apu_tick`) and the **audio callback thread** (which the OS fires when it needs samples). Getting this wrong produces crackling, tearing, or crashes.

#### Design

A ring buffer is a fixed-size array used as a circular queue. Two indices — `read_pos` and `write_pos` — chase each other around the array. The writer advances `write_pos`; the reader advances `read_pos`. When `write_pos == read_pos` the buffer is empty; when they're one slot apart (wrapping) it's full.

```
    read_pos          write_pos
       ↓                 ↓
[ _, _, S, S, S, S, S, _, _, _ ]
         ↑─────────────↑
         unconsumed samples
```

#### Implementation in C

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define RING_BUFFER_SIZE 4096   // must be a power of 2
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

typedef struct {
    int16_t  samples[RING_BUFFER_SIZE];
    volatile uint32_t write_pos;   // written by emulation thread
    volatile uint32_t read_pos;    // written by audio thread
} RingBuffer;

static RingBuffer ring;

void ring_buffer_init(void) {
    memset(&ring, 0, sizeof(ring));
}

// Returns the number of samples currently available to read.
static inline uint32_t ring_buffer_available(void) {
    return (ring.write_pos - ring.read_pos) & RING_BUFFER_MASK;
}

// Returns how many slots are free to write.
static inline uint32_t ring_buffer_free_space(void) {
    return RING_BUFFER_SIZE - 1 - ring_buffer_available();
}

// Called from the emulation thread (apu_tick).
// Converts the float mix output [-1.0, 1.0] → int16_t and enqueues it.
// Drops the sample silently if the buffer is full (prevents emulation slowdown).
void ring_buffer_push(float sample) {
    if (ring_buffer_free_space() == 0) {
        // Buffer full: audio thread is not consuming fast enough.
        // Drop this sample rather than blocking the emulation.
        return;
    }

    // Clamp to [-1.0, 1.0] before scaling to avoid int16 overflow.
    if (sample >  1.0f) sample =  1.0f;
    if (sample < -1.0f) sample = -1.0f;

    int16_t pcm = (int16_t)(sample * 32767.0f);
    ring.samples[ring.write_pos & RING_BUFFER_MASK] = pcm;

    // Memory barrier: ensure the sample write is visible before the index update.
    // On x86 this is a no-op but it documents the intent and matters on ARM.
    __sync_synchronize();

    ring.write_pos++;
}

// Called from the audio callback thread.
// Returns 0 (silence) if the buffer is empty (prevents audio callback from blocking).
int16_t ring_buffer_pop(void) {
    if (ring_buffer_available() == 0) {
        // Buffer empty: emulation is running behind.
        // Return silence rather than blocking the audio callback.
        return 0;
    }

    int16_t pcm = ring.samples[ring.read_pos & RING_BUFFER_MASK];

    __sync_synchronize();

    ring.read_pos++;
    return pcm;
}
```

#### Why `volatile` and `__sync_synchronize`?

The two threads share `read_pos` and `write_pos`. Without `volatile`, the compiler may cache these in a register and never re-read them from memory — the audio thread would read a stale `write_pos` forever and always see an empty buffer.

`__sync_synchronize()` is a full memory barrier. It tells the CPU not to reorder memory operations across that point. On x86 this is basically free (the architecture already provides strong ordering), but on ARM and other weakly-ordered architectures it prevents the CPU from making the index update visible before the sample write.

This implementation is a **single-producer single-consumer** (SPSC) ring buffer, which is the one case where you don't need a mutex. Only one thread writes `write_pos`, only one reads `read_pos`. That's enough for correctness without locking.

#### Choosing the buffer size

`4096` samples at 44100 Hz = ~93 ms of audio. That's enough headroom for typical emulation timing jitter without being so large that it adds noticeable latency. If you get underruns (silence gaps), increase it. If you notice input latency, decrease it.

#### The audio callback (SDL2 wiring)

```c
void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int num_samples = len / sizeof(int16_t);
    for (int i = 0; i < num_samples; i++) {
        out[i] = ring_buffer_pop();  // returns 0 on underrun
    }
}

void audio_init(void) {
    ring_buffer_init();

    SDL_AudioSpec want = {0};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;   // callback fires every 512 samples (~11.6 ms)
    want.callback = audio_callback;
    want.userdata = NULL;

    SDL_AudioSpec got;
    SDL_OpenAudio(&want, &got);
    SDL_PauseAudio(0);  // start playing
}
```

---

### Step 2: The frame sequencer

The frame sequencer is a state machine with 4 or 5 steps (controlled by bit 7 of `$4017`). It fires at CPU cycle multiples:

**4-step mode** (default):
- Step 1 at cycle 3728: clock envelopes + triangle linear counter
- Step 2 at cycle 7456: clock envelopes + linear counter + length counters + sweeps
- Step 3 at cycle 11185: clock envelopes + linear counter
- Step 4 at cycle 14914: clock envelopes + length counters + sweeps + **set IRQ flag**

**5-step mode** (`$4017` bit 7 = 1):
- Steps 1–3 same as above
- Step 4 at cycle 14914: clock envelopes + linear counter + length counters + sweeps (no IRQ)
- Step 5 at cycle 18640: nothing happens (skip)

Implement this as a cycle counter that resets and triggers the appropriate clocks.

### Step 3: Pulse channels (implement both together — they're identical)

```c
typedef struct {
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
} Pulse;
```

The 4 duty cycle waveforms (8 steps each):

```c
static const uint8_t DUTY_TABLE[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},  // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0},  // 25%
    {0, 1, 1, 1, 1, 0, 0, 0},  // 50%
    {1, 0, 0, 1, 1, 1, 1, 1},  // 25% negated
};
```

Each APU clock (every 2 CPU cycles), decrement `timer_current`. When it hits zero, reload it from `timer_period` and advance `seq_pos = (seq_pos + 1) & 7`.

Output is `DUTY_TABLE[duty][seq_pos] * volume_level`. The channel is **muted** (output 0) if: length counter is zero, timer period < 8, or timer period > 0x7FF.

The sweep unit: every time the frame sequencer fires the "half frame" clock, check if sweep divider expired and sweep is enabled. Compute the target period: shift the current period right by `sweep_shift`, then add or subtract. If negating on pulse 1, use one's complement (add 1). Pulse 2 uses two's complement. If target period > 0x7FF, mute the channel. Apply the period change if enabled and divider expired.

### Step 4: Triangle channel

The triangle has no volume control — it's either on or off. It outputs a 32-step triangle waveform:

```c
static const uint8_t TRIANGLE_TABLE[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
};
```

The triangle clocks its sequencer **every CPU cycle** (not every 2), making it twice the frequency resolution of pulse channels. It has a **linear counter** instead of an envelope — a simple countdown loaded by `$4008` bit 6 and the value in bits 0–6. Both the linear counter and the length counter must be nonzero for the channel to produce sound.

The triangle also mutes at timer period < 2 (ultrasonic suppression).

### Step 5: Noise channel

The noise channel uses a 15-bit linear feedback shift register (LFSR). Each timer expiry, it shifts right and XORs bits together:

```c
static uint16_t lfsr = 1; // initial value — never 0, a zero LFSR is stuck

void noise_clock_timer(void) {
    uint16_t feedback;
    if (noise_mode) { // bit 7 of $400E
        feedback = ((lfsr >> 0) ^ (lfsr >> 6)) & 1;
    } else {
        feedback = ((lfsr >> 0) ^ (lfsr >> 1)) & 1;
    }
    lfsr = (lfsr >> 1) | (feedback << 14);
}
```

Output is `(lfsr & 1) == 0 ? volume : 0`. The noise period is set by a lookup table:

```c
static const uint16_t NOISE_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};
```

### Step 6: The mixer

The NES APU uses a **non-linear mix** due to the resistor ladder in the real hardware:

```c
float apu_mix(void) {
    uint8_t p1 = pulse1_output();   // 0-15
    uint8_t p2 = pulse2_output();   // 0-15
    uint8_t tr = triangle_output(); // 0-15
    uint8_t no = noise_output();    // 0-15
    uint8_t dm = dmc_output();      // 0-127

    float pulse_out = 0.0f;
    if (p1 + p2 > 0)
        pulse_out = 95.88f / (8128.0f / (p1 + p2) + 100.0f);

    float tnd_out = 0.0f;
    if (tr + no + dm > 0)
        tnd_out = 159.79f / (1.0f / (tr/8227.0f + no/12241.0f + dm/22638.0f) + 100.0f);

    return pulse_out + tnd_out; // range roughly 0.0 to ~0.95
}
```

Scale this to your output format: `(int16_t)(sample * 32767.0f)` for 16-bit signed. This is already done inside `ring_buffer_push`.

### Step 7: Register writes

The CPU writes to APU registers at `$4000`–`$4017`. Here's the register map per channel:

```
$4000/$4004  [DDLCVVVV]  Pulse 1/2: duty, length halt, constant vol, volume
$4001/$4005  [EPPPNSSS]  Pulse 1/2: sweep enable, period, negate, shift
$4002/$4006  [LLLLLLLL]  Pulse 1/2: timer low 8 bits
$4003/$4007  [LLLLLHHH]  Pulse 1/2: length counter load, timer high 3 bits
$4008        [CRRRRRRR]  Triangle: control/length halt, linear counter load
$400A        [LLLLLLLL]  Triangle: timer low
$400B        [LLLLLHHH]  Triangle: length counter load, timer high
$400C        [--LCVVVV]  Noise: length halt, constant vol, volume
$400E        [M---PPPP]  Noise: mode, period
$400F        [LLLLL---]  Noise: length counter load
$4015        [---D NT21]  Status: enable channels / read gives IRQ+length status
$4017        [MI------]  Frame counter: mode, IRQ inhibit
```

When `$4003`/`$4007`/`$400B`/`$400F` is written, the length counter is reloaded from the length table AND the sequencer position resets to 0 (for pulse channels) and the envelope restart flag is set.

```c
static const uint8_t LENGTH_TABLE[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
```

### Step 8: DMC (last — defer it)

The DMC plays back 1-bit delta-encoded samples from memory. It requires DMA reads from CPU address space and can steal CPU cycles. It's complex enough to defer — you can ship a game-playable APU without it. Implement it last.

---

## Register write timing quirk

When the CPU writes to `$4003` (pulse 1 length/timer high), the **phase of the sequencer resets to 0**. This is the "phase reset on write" behavior — it's why games can reliably produce clean attacks on notes. Forgetting this makes pulse channels sound buzzy on sharp note onsets.

---

## Testing strategy (without PPU)

### Stage 0 — silence (do first)
Confirm your audio pipeline produces silence without crashing. No APU logic yet. If you can open an audio device and fill zeros for 5 seconds, you're ready.

### Stage 1 — single pulse tone
Hard-code register values for a pulse channel: duty=2 (50%), timer period=`0x01AB` (roughly A4=440Hz), constant volume=15, length halt=1 so it doesn't expire:

```c
// In your init/test function:
apu_write(0x4000, 0b10111111); // duty 2, length halt, constant vol=15
apu_write(0x4002, 0xAB);       // timer low
apu_write(0x4003, 0x00);       // timer high = 0, length = 0
apu_write(0x4015, 0x01);       // enable pulse 1
```

You should hear a buzzy square wave tone at ~440 Hz. No ROM needed.

### Stage 2 — frequency accuracy
Use a tuner app on your phone (or Audacity's frequency analysis). The tone should land within 1 Hz of the expected frequency. Mistakes here mean your clock math is wrong — double-check `cycles_per_sample` and the timer clock rate.

### Stage 3 — envelope
Set `constant_vol=0`, `envelope_vol=15`. Each time you write to `$4003`, you should hear the volume decay from loud to silent over about a second.

### Stage 4 — length counter
Set `length_halt=0`, write a length value. The note should cut off after a predictable time. Write different length values and confirm they produce proportionally different durations.

### Stage 5 — triangle + noise
Add triangle and noise channels. A triangle at low timer periods + noise at period 6–8 will sound like a primitive kick drum pattern when you sequence them manually.

### Stage 6 — run a simple ROM
Find the `apu_test` ROM by blargg. It writes specific registers and checks APU status reads. Run it with your dummy PPU — watch the APU `$4015` reads in your CPU trace. There's also `test_apu_env.nes` specifically for envelope behavior.

### Feel-good milestone target
Load **Super Mario Bros.** ROM. The title screen music uses both pulses + triangle. Even without PPU rendering you should hear it play. The theme is iconic enough that you'll know immediately if pitch, timing, and mixing are correct.

---

## Common mistakes to avoid

**Off-by-one on timer clocking.** Pulse channels clock every 2 CPU cycles. Triangle clocks every CPU cycle. Getting this backward doubles or halves your pitch.

**Not silencing on timer period < 8 (pulse).** Very low periods produce ultrasonic frequencies that cause aliasing — the hardware mutes them, so must you.

**Forgetting the LFSR starts at 1, not 0.** An LFSR of all zeros is stuck forever.

**Linear mixer instead of the resistor ladder formula.** Without it your output will clip and sound harsh.

**Writing to `$4017` resets the frame sequencer.** Many games write it on startup to ensure a clean state. If you don't handle this, frame sequencer timing will be off by up to 7457 cycles.

**Ring buffer race condition.** Never write `write_pos` from the audio thread or `read_pos` from the emulation thread. SPSC only works if ownership of each index is strictly respected.

---

## Recommended implementation order

1. Audio plumbing + ring buffer → silence plays correctly
2. Frame sequencer skeleton (just the counter, no effects yet)
3. Pulse 1 with constant volume, no envelope, no sweep → you hear a tone
4. Pulse 2 → second voice works
5. Envelope on both pulses → volume decay works
6. Length counters → notes cut off correctly
7. Sweep units → pitch bends work
8. Triangle → bass line works
9. Noise → drums/effects work
10. Mixer formula → levels balanced correctly
11. Run blargg's `apu_test` ROM
12. **Load Super Mario Bros. → hear the theme → feel good**
13. DMC → sampled voices (deferrable)

The main thing to internalize before writing code: **clock hierarchy first, waveform generation second**. Most APU bugs come from getting the timer clock rate or the frame sequencer timing wrong — and those bugs are silent (literally). Get the clocks right and the rest follows.
