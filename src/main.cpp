#include <SDL.h>
#include <cstdio>
#include "instruction.h"

void init_lookup() {
    for (int i = 0; i < 256; i++) {
        lookup[i] = (instruction){
            "???", op_NOOP, addr_IMP, 1, 2
        };
    }
    // NOP
    lookup[0xEA] = (instruction){ "NOP", op_NOOP, addr_IMP, 1, 2 };
    // LDA
    lookup[0xA9] = (instruction){ "LDA", op_LDA, addr_IMM, 2, 2 };
    lookup[0xA5] = (instruction){ "LDA", op_LDA, addr_ZPO, 2, 3 };
    lookup[0xB5] = (instruction){ "LDA", op_LDA, addr_ZPX, 2, 4 };
    lookup[0xAD] = (instruction){ "LDA", op_LDA, addr_ABS, 2, 4 };
    lookup[0xBD] = (instruction){ "LDA", op_LDA, addr_ABX, 2, 4 };
    lookup[0xB9] = (instruction){ "LDA", op_LDA, addr_ABY, 2, 4 };
    lookup[0xA1] = (instruction){ "LDA", op_LDA, addr_IDX, 2, 6 };
    lookup[0xB1] = (instruction){ "LDA", op_LDA, addr_IZY, 2, 5 };
    // LDX
    lookup[0xA2] = (instruction){ "LDX", op_LDX, addr_IMM, 2, 2 };
    lookup[0xA6] = (instruction){ "LDX", op_LDX, addr_ZPO, 2, 3 };
    lookup[0xB6] = (instruction){ "LDX", op_LDX, addr_ZPY, 2, 4 };
    lookup[0xAE] = (instruction){ "LDX", op_LDX, addr_ABS, 2, 4 };
    lookup[0xBE] = (instruction){ "LDX", op_LDX, addr_ABY, 2, 4 };
    // LDY
    lookup[0xA0] = (instruction){ "LDY", op_LDY, addr_IMM, 2, 2 };
    lookup[0xA4] = (instruction){ "LDY", op_LDY, addr_ZPO, 2, 3 };
    lookup[0xB4] = (instruction){ "LDY", op_LDY, addr_ZPX, 2, 4 };
    lookup[0xAC] = (instruction){ "LDY", op_LDY, addr_ABS, 2, 4 };
    lookup[0xBC] = (instruction){ "LDY", op_LDY, addr_ABX, 2, 4 };

}

void init() {
    init_lookup();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }
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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    init();
    SDL_Window* window = setup_window();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // 60 fps
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
