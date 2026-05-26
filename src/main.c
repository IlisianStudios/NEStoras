
#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "instruction.h"
#include "nestest_compare.h"
#include "ringbuffer.h"
#include "debug_window.h"
#include "ppu.h"

bool running = true;
CPU cpu;
static SDL_AudioDeviceID audio_dev = 0;
Timing timing;
static bool cpu_paused = false;
static bool cpu_step_one = false;  // advance exactly one instruction

static SDL_Renderer *main_renderer = NULL;
static SDL_Texture  *frame_tex = NULL;

void debug_nestest(void) {
    printf("DEBUG mode enabled\n");
    if(!cartridge_load("nestest.nes"))return;
    printf("Nestest rom loaded successfully\n");
    cpu_reset(&cpu);

    cpu.pc = 0xC000;
    cpu.testing_mode = true;
    cpu.nestest_comp = true;
    apu_dbg.enabled = true;
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

    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = NULL;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    if (audio_dev == 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
    } else {
        printf("Audio opened: freq=%d, format=0x%04X, channels=%d, samples=%d\n",
               have.freq, have.format, have.channels, have.samples);
        apu_set_output_sample_rate(have.freq);
        SDL_PauseAudioDevice(audio_dev, 0);
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
    ring_buffer_init();
    apu_init();
    ppu_init();
    apu_debug_reset();
    init_audio();
}

static void init_renderer(SDL_Window *window) {
    // No vsync — the audio ring buffer drives our pacing (AUDIO_SYNC).
    main_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!main_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_RenderSetLogicalSize(main_renderer, PPU_FRAME_W, PPU_FRAME_H);

    frame_tex = SDL_CreateTexture(main_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        PPU_FRAME_W, PPU_FRAME_H);
    if (!frame_tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(1);
    }
}

static void present_frame(void) {
    SDL_UpdateTexture(frame_tex, NULL, ppu_framebuffer, PPU_FRAME_W * sizeof(uint32_t));
    SDL_RenderClear(main_renderer);
    SDL_RenderCopy(main_renderer, frame_tex, NULL, NULL);
    SDL_RenderPresent(main_renderer);
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
            if (!cartridge_load(event->drop.file))
                return;

            apu_init();
            ppu_init();
            cpu_reset(&cpu);
            cpu.nestest_comp = false;
            cpu.nestest_passed = false;
            timing_init(&timing, AUDIO_SYNC);
            ring_buffer_init();
            apu_debug_reset();

            // If the debug window is open, preserve testing mode
            if (debug_window_visible()) {
                cpu.testing_mode = true;
                apu_dbg.enabled = true;
            }

            const char *filename = strrchr(event->drop.file, '/');
            filename = filename ? filename + 1 : event->drop.file;
            printf("ROM loaded: %s\n", filename);

            if (strcmp(filename, "nestest.nes") == 0) {
                cpu.pc = 0xC000;
                cpu.testing_mode = true;
                apu_dbg.enabled = true;
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
        // Let the debug window handle its own events first
        if (debug_window_handle_event(&event))
            continue;

        // NES controller bit layout: A B Select Start Up Down Left Right
        //                             7 6   5      4   3    2     1    0
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            bool pressed = (event.type == SDL_KEYDOWN);
            uint8_t mask = 0;
            switch (event.key.keysym.sym) {
                case SDLK_z:      mask = 0x80; break;  // A
                case SDLK_x:      mask = 0x40; break;  // B
                case SDLK_RSHIFT:
                case SDLK_LSHIFT: mask = 0x20; break;  // Select
                case SDLK_RETURN: mask = 0x10; break;  // Start
                case SDLK_UP:     mask = 0x08; break;  // Up
                case SDLK_DOWN:   mask = 0x04; break;  // Down
                case SDLK_LEFT:   mask = 0x02; break;  // Left
                case SDLK_RIGHT:  mask = 0x01; break;  // Right
                default: break;
            }
            if (mask) {
                if (pressed) controller_state[0] |= mask;
                else         controller_state[0] &= ~mask;
            }

        }
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    try_free_cartridge();
                    nestest_close(&nestest_log);
                    running = false;
                    break;
                case SDLK_d:
                    debug_window_toggle();
                    break;
                case SDLK_r:
                    cpu_paused = !cpu_paused;
                    debug_log(cpu_paused ? "--- CPU PAUSED ---" : "--- CPU RESUMED ---");
                    break;
                case SDLK_q:
                    if (cpu_paused) {
                        cpu_step_one = true;
                    }
                    break;
                default:
                    break;
            }
        }
        handle_event_type(&event);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    init();
    SDL_Window* window = setup_window();
    init_renderer(window);
    debug_window_init(window, &cpu.testing_mode);

#ifndef NDEBUG
    debug_nestest();
    debug_window_toggle();
#endif

    if (argc > 1) {
        printf("Loading ROM from command line: %s\n", argv[1]);
        if (cartridge_load(argv[1])) {
            apu_init();
            ppu_init();
            cpu_reset(&cpu);
            ring_buffer_init();
            apu_debug_reset();
        }
    }

    timing_init(&timing, AUDIO_SYNC);
    while (running) {
        SDL2_loop();

        if (cartridge == NULL) {
            debug_window_set_paused(cpu_paused);
            debug_window_update(&cpu);
            SDL_Delay(10);
            continue;
        }

        uint32_t cycles_to_run = timing_update(&timing);

        if (cycles_to_run == 0) {
            SDL_Delay(1);
            continue;
        }

        if (cpu_paused && !cpu_step_one) {
            debug_window_set_paused(cpu_paused);
            debug_window_update(&cpu);
            SDL_Delay(1);
            continue;
        }

        if (cpu_step_one) {
            cpu_step_one = false;
            run_cycles(&cpu, 1);
        } else if (cycles_to_run == UINT32_MAX) {
            run_cycles(&cpu, 1);
        } else {
            run_cycles(&cpu, cycles_to_run);
        }

        apu_debug_print(&cpu);

        if (ppu_frame_ready()) {
            ppu_clear_frame_ready();
            present_frame();
        }

        debug_window_set_paused(cpu_paused);
        debug_window_update(&cpu);

        if (cpu.nestest_passed) {
            cpu.nestest_passed = false;
            debug_log("[NESTEST] completed — drop a ROM to continue");
            try_free_cartridge();
        }

    }

    try_free_cartridge();
    debug_window_destroy();
    if (frame_tex)      SDL_DestroyTexture(frame_tex);
    if (main_renderer)  SDL_DestroyRenderer(main_renderer);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
