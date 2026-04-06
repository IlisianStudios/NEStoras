#pragma once
#ifndef NESTORAS_DEBUG_WINDOW_H
#define NESTORAS_DEBUG_WINDOW_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup (after SDL_Init). Returns false on failure.
bool debug_window_init(void);

// Call once at shutdown.
void debug_window_destroy(void);

// Toggle visibility (D key).
void debug_window_toggle(void);

// Returns true when the window is currently shown.
bool debug_window_visible(void);

// Redraw the debug overlay.  Call once per main-loop iteration.
// Reads CPU, APU, and bus state directly via their externs.
void debug_window_update(const CPU *cpu);

// Let the debug window handle its own SDL events (window close, etc.)
// Returns true if the event was consumed.
bool debug_window_handle_event(const SDL_Event *event);

#ifdef __cplusplus
}
#endif

#endif // NESTORAS_DEBUG_WINDOW_H


