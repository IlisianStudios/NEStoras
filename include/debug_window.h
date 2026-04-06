#pragma once
#ifndef NESTORAS_DEBUG_WINDOW_H
#define NESTORAS_DEBUG_WINDOW_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup, after SDL_Init and after creating the main window.
// testing_mode points to cpu.testing_mode so the debug window can toggle it.
bool debug_window_init(SDL_Window *main_window, bool *testing_mode);

void debug_window_destroy(void);

// Toggle visibility + testing mode (D key).
void debug_window_toggle(void);

bool debug_window_visible(void);

// Redraw. Call once per main-loop iteration (self-throttled, no-op when hidden).
void debug_window_update(const CPU *cpu);

// Let the debug window consume its own SDL events (close button, etc.).
bool debug_window_handle_event(const SDL_Event *event);

// Append a formatted line to the scrolling log (top half of the window).
void debug_log(const char *fmt, ...);

// Set from main.c so the footer can show pause state.
void debug_window_set_paused(bool paused);

#ifdef __cplusplus
}
#endif

#endif // NESTORAS_DEBUG_WINDOW_H


