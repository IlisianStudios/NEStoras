#include "ringbuffer.h"

#include <string.h>

RingBuffer ring;

uint32_t ring_buffer_available(void) {
    return (ring.write_pos - ring.read_pos) & RING_BUFFER_MASK;
}

uint32_t ring_buffer_free_space(void) {
    return RING_BUFFER_SIZE - 1 - ring_buffer_available();
}

void ring_buffer_push(float sample) {
    if (ring_buffer_free_space() == 0) return;

    if (sample >  1.0f) sample =  1.0f;
    if (sample < -1.0f) sample = -1.0f;

    int16_t pcm = (int16_t)(sample * 32767.0f);
    ring.samples[ring.write_pos & RING_BUFFER_MASK] = pcm;

    __sync_synchronize();

    ring.write_pos++;
}

int16_t ring_buffer_pop(void) {
    if (ring_buffer_available() == 0) return 0;

    int16_t pcm = ring.samples[ring.read_pos & RING_BUFFER_MASK];

    __sync_synchronize();

    ring.read_pos++;
    return pcm;
}

void ring_buffer_init(void) {
    memset(&ring, 0, sizeof(ring));
}
