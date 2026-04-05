#include "apu.h"

#include "ringbuffer.h"

void apu_step(void) {

    sample_accumulator += 1.0;
    if (sample_accumulator >= cycles_per_sample) {
        sample_accumulator -= cycles_per_sample;
        float sample = apu_mix();          // get current output
        ring_buffer_push(sample);          // push to audio thread
    }
}

float apu_mix(void) {
    return 0;
}
