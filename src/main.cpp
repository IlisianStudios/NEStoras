#include <cstdio>
#include <SDL2/SDL.h>
#include <stdio.h>

#include "bus.h"
#include "cartridge.h"
#include "instruction.h"

bool running = true;
CPU cpu;
// in main.cpp

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


void try_free_cartridge() {
    if (cartridge != nullptr) {
        cartridge_free(cartridge);
        cartridge = nullptr;
    }
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
            cartridge =  static_cast<Cartridge *>(malloc(sizeof(Cartridge)));
            if (!cartridge_load(cartridge, event->drop.file)) {
                free(cartridge);
                cartridge = nullptr;
            }
            else {
                cpu_reset(&cpu);
                cpu.testing_mode = true;
            }
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
