#pragma once

#ifndef NESTORAS_APU_H
#define NESTORAS_APU_H

#endif //NESTORAS_APU_H

static double sample_accumulator = 0.0;
static const double cycles_per_sample = 1789773.0 / 44100.0;

void apu_step(void);
float apu_mix(void);

