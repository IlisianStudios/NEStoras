#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#include "apu.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "instruction.h"
#include "nestest_compare.h"
#include "ringbuffer.h"

bool running = true;
static CPU cpu;

void debug_nestest(void) {
    printf("DEBUG mode enabled\n");
    if(!cartridge_load("nestest.nes"))return;
    printf("Nestest rom loaded successfully\n");
    cpu_reset(&cpu);

    cpu.pc = 0xC000;
    cpu.testing_mode = true;
    cpu.nestest_comp = true;
    if(!nestest_open(&nestest_log, "nestest.log")) {
        printf("nestest.log could not be opened\n");
        return;
    }
    printf("Nestest cpu set proper\n");
}

void init_audio(void) {
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);

    want.freq = 44100;          // 44.1 kHz
    want.format = AUDIO_S16SYS; // Signed 16-bit, system byte order
    want.channels = 1;          // Mono
    want.samples = 2048;        // Buffer size (must be power of 2)
    want.callback = audio_callback;
    want.userdata = NULL;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    if (dev == 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
    } else {
        // 5. Unpause to start audio
        SDL_PauseAudioDevice(dev, 0);

        // Keep the program alive to hear sound
        SDL_Delay(5000);

        SDL_CloseAudioDevice(dev);
    }

}

void init(void) {
    init_lookup();
    cartridge = NULL;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    init_audio();
    ring_buffer_init();
    apu_init();
}

SDL_Window* setup_window(void) {
    SDL_Window* window = SDL_CreateWindow(
        "NEStoras",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        256 * 3,
        256 * 3,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(1);
    }

    return window;
}

void handle_event_type(const SDL_Event *event) {
    switch (event->type) {
        case SDL_QUIT: {
            nestest_close(&nestest_log);
            try_free_cartridge();
            running = false;
            break;
        }
        case SDL_DROPFILE: {
            printf("Dropping file: %s\n", event->drop.file);
            // User dropped a file
            if (!cartridge_load(event->drop.file))
                return;

            cpu_reset(&cpu);
            const char *filename = strrchr(event->drop.file, '/');
            filename = filename ? filename + 1 : event->drop.file;
            if (strcmp(filename, "nestest.nes") == 0) {
                cpu.pc = 0xC000;
                cpu.testing_mode = true;
            }
            SDL_free(event->drop.file);
            break;
        }
        default:break;
    }

}

void SDL2_loop(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            try_free_cartridge();
            nestest_close(&nestest_log);
            running = false;
        }
        handle_event_type(&event);
    }

    // 60 fps
    // SDL_Delay(16);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    init();
    SDL_Window* window = setup_window();

#ifndef NDEBUG
    debug_nestest();
#endif

    timing_init(&timing, FIXED);
    while (running) {
        SDL2_loop();

        if (cartridge == NULL) continue;

        uint32_t cycles_to_run = timing_update(&timing);

        if (cycles_to_run == UINT32_MAX)
            run_cycles(&cpu, 1);
        else
            run_cycles(&cpu, cycles_to_run);

    }

    try_free_cartridge();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
