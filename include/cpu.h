#pragma once
#include <stdint.h>

typedef struct{
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp; // stack pointer $0100-$01FF
    uint16_t pc;
    uint8_t status;

} CPU;