#include "ringbuffer.h"

#include <stdatomic.h>
#include <string.h>
#include <SDL2/SDL.h>

RingBuffer ring;

uint32_t ring_buffer_available(void) {
    // No mask here — the raw difference is the true fill level.
    // Indices use & RING_BUFFER_MASK only when accessing the array.
    return ring.write_pos - ring.read_pos;
}

uint32_t ring_buffer_free_space(void) {
    return RING_BUFFER_SIZE - ring_buffer_available();
}

void ring_buffer_push(float sample) {
    // Block instead of drop: the audio callback consumes at 44100 Hz,
    // so this back-pressure naturally paces the emulator to audio rate.
    while (ring_buffer_free_space() == 0) {
        SDL_Delay(1);
    }

    if (sample >  1.0f) sample =  1.0f;
    if (sample < -1.0f) sample = -1.0f;

    int16_t pcm = (int16_t)(sample * 32767.0f);
    ring.samples[ring.write_pos & RING_BUFFER_MASK] = pcm;

    atomic_thread_fence(memory_order_seq_cst);

    ring.write_pos++;
}

int16_t ring_buffer_pop(void) {
    if (ring_buffer_available() == 0) return 0;

    int16_t pcm = ring.samples[ring.read_pos & RING_BUFFER_MASK];

    atomic_thread_fence(memory_order_seq_cst);

    ring.read_pos++;
    return pcm;
}

void ring_buffer_init(void) {
    memset(&ring, 0, sizeof(ring));
}
