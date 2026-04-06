#pragma once

#ifndef NESTORAS_RINGBUFFER_H
#define NESTORAS_RINGBUFFER_H
#include <stdint.h>

#endif //NESTORAS_RINGBUFFER_H
#include <stdio.h>

#define RING_BUFFER_SIZE 4096   // must be a power of 2
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

void ring_buffer_push(float sample);
int16_t ring_buffer_pop(void);

typedef struct {
    int16_t  samples[RING_BUFFER_SIZE];
    volatile uint32_t write_pos;   // written by emulation thread
    volatile uint32_t read_pos;    // written by audio thread
} RingBuffer;

extern RingBuffer ring;

uint32_t ring_buffer_available(void);
uint32_t ring_buffer_free_space(void);

void ring_buffer_init(void);

