#include <stdint.h>
#include <stdbool.h>

uint8_t ram[2048];

// Controller state — bits: A B Select Start Up Down Left Right
uint8_t controller_state[2] = {0, 0};
// Shift registers for serial read
uint8_t controller_shift[2] = {0, 0};
bool controller_strobe = false;
