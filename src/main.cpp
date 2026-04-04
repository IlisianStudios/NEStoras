#include <cstdio>
#include <SDL2/SDL.h>
#include <stdio.h>

#include "bus.h"
#include "cartridge.h"
#include "instruction.h"

bool running = true;
CPU cpu;
// in main.cpp

void debug_nestest() {
    printf("DEBUG mode enabled\n");
    if(!cartridge_load("nestest.log"))return;
    printf("Nestest loaded successfully\n");
    cpu_reset(&cpu);

    cpu.pc = 0xC000;
    cpu.testing_mode = true;
    cpu.nestest_comp = true;
    printf("Nestest cpu set proper\n");
}

void init() {
    init_lookup();
    cartridge = nullptr;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
}

SDL_Window* setup_window() {
    SDL_Window* window = SDL_CreateWindow(
        "NEStoras",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        256 * 3,
        256 * 3,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        exit(1);
    }

    return window;
}

void handle_event_type(const SDL_Event *event) {
    switch (event->type) {
        case SDL_QUIT: {
            try_free_cartridge();
            running = false;
            break;
        }
        case SDL_DROPFILE: {
            std::printf("Dropping file: %s\n", event->drop.file);
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

void SDL2_loop() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            try_free_cartridge();
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


    while (running) {
        SDL2_loop();
        if (cartridge != nullptr) {
            cpu_step(&cpu);
        }
    }

    try_free_cartridge();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
