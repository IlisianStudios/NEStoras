# NEStoras [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=IlisianStudios_NEStoras&metric=alert_status&token=51408dadf272b8c9ea1f61f6fa7442ec6bc49649)](https://sonarcloud.io/summary/new_code?id=IlisianStudios_NEStoras)

A NES (Nintendo Entertainment System) emulator written in **C** with SDL2 including a debugger.

![Alt text](./screenshots/both.jpg)

## Table of Contents

- [Supported Mappers](#supported-mappers)
- [Status](#status)
- [Contributing](#contributing)
- [Build](#build)
  - [macOS](#macos)
  - [Windows](#windows)
  - [Linux](#linux)
- [Run](#run)
- [Controls](#controls)
- [Documentation](#documentation)
- [Project Layout](#project-layout)

## Supported Mappers


| Mapper | Name | Supported Games                                          |
| ------ | ---- | -------------------------------------------------------- |
| 0      | NROM | [Supported games](https://nesdir.github.io/mapper0.html) |

## Status


| Subsystem                             | State                                                        |
| ------------------------------------- | ------------------------------------------------------------ |
| CPU (Ricoh 2A03 / 6502)               | Working — passes`nestest.nes`                               |
| Memory bus + mirroring                | Working                                                      |
| Controller 1 (keyboard)               | Working                                                      |
| iNES ROM loading                      | Working                                                      |
| Mapper 0 (NROM)                       | Working                                                      |
| Other mappers (MMC1, MMC3, UxROM, …) | Not yet                                                      |
| APU (pulse 1/2, triangle, noise, DMC) | Working — audio via SDL2                                    |
| PPU (Ricoh 2C02)                      | Working — cycle-accurate, catch-up step model               |
| OAMDMA (`$4014`)                      | Working                                                      |
| Sprite 0 hit / sprite overflow        | Working                                                      |
| NTSC + PAL timing                     | Working                                                      |
| Save states                           | Not yet                                                      |
| Built-in debug window                 | Working (PPU panel, sprite viewer, CHR + palette, APU scope) |

SMB plays through. See [PPU_SPEC.md](PPU_SPEC.md) for the PPU design notes.

## Contributing

Contributions are welcome and encouraged — bug fixes, accuracy improvements,
documentation, and especially **new mapper implementations**. The vast majority
of the NES library lives outside Mapper 0, so adding MMC1, UxROM, CNROM, MMC3,
and friends is the single most impactful way to expand what NEStoras can run.

If you'd like to help:

- Open an issue describing what you'd like to tackle (especially for larger
  changes) so we can coordinate.
- Fork, branch off `main`, and open a pull request. Match the existing C style
  (see the source for conventions) and keep changes focused.
- For mappers, please include at least one test ROM result or a short note on
  which commercial titles you verified against.

## Build

Requires SDL2, SDL2_ttf, CMake, and a C11 compiler.

### macOS

```bash
brew install cmake sdl2 sdl2_ttf
xcode-select --install

# Build
cmake -S . -B build
cmake --build build
```

If CMake can't find SDL2, point it explicitly:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2);$(brew --prefix sdl2_ttf)"
```

### Windows

Using [MSYS2](https://www.msys2.org/) (MinGW-w64 UCRT64 shell):

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
                   mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-SDL2 \
                   mingw-w64-ucrt-x86_64-SDL2_ttf

cmake -S . -B build -G "Ninja"
cmake --build build
```

Using Visual Studio (MSVC) with [vcpkg](https://vcpkg.io/):

```powershell
vcpkg install sdl2 sdl2-ttf

cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Linux

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libsdl2-ttf-dev

cmake -S . -B build
cmake --build build
```

Fedora:

```bash
sudo dnf install gcc cmake SDL2-devel SDL2_ttf-devel

cmake -S . -B build
cmake --build build
```

Arch:

```bash
sudo pacman -S base-devel cmake sdl2 sdl2_ttf

cmake -S . -B build
cmake --build build
```

## Run

Drop a `.nes` file onto the window, or pass it as a command-line argument:

```bash
./build/NEStoras path/to/rom.nes
```

In a debug build the emulator auto-loads `nestest.nes` from the build directory
and opens the debug window. Press `Esc` to quit.

## Controls


| Key        | NES Button |
| ---------- | ---------- |
| Z          | A          |
| X          | B          |
| Shift      | Select     |
| Enter      | Start      |
| Arrow keys | D-pad      |

Emulator controls:


| Key | Action                                  |
| --- | --------------------------------------- |
| D   | Toggle debug window                     |
| R   | Pause / resume CPU                      |
| Q   | Step one CPU instruction (while paused) |
| Esc | Quit                                    |

The debug window shows: CPU instruction log, PPU state (frame/scanline/dot,
Loopy `v`/`t`/`x`/`w`, CTRL/MASK/STAT, NMI count, sprite-0-hit/overflow
flags), a visual 64-sprite OAM viewer, both CHR pattern tables, the palette
RAM as colour swatches, APU per-channel scopes (P1/P2/TRI/NOI/DMC + mix), and
the channel/length/volume state for each APU voice.

![Alt text](./screenshots/debugger.png)

## Documentation


| Document                             | Contents                                                                                                                     |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| [DESIGN.md](DESIGN.md)               | Hardware reference: CPU/PPU/APU specs, memory maps, register tables, CPU implementation walkthrough, development roadmap     |
| [PPU_SPEC.md](PPU_SPEC.md)           | PPU implementation spec — cycle-accurate dot stepping, sprite evaluation, the catch-up integration model, debug-window plan |
| [nes_apu_guide.md](nes_apu_guide.md) | APU implementation notes                                                                                                     |

## Project Layout

```
include/          public headers (one per subsystem)
src/              implementation
  main.c              SDL setup, main loop, event handling
  cpu.c               6502 core, run_cycles, NMI/IRQ service
  instruction.c       6502 opcode table + addressing modes
  bus.c               extern CPU-bus globals (RAM, controllers)
  cartridge.c         iNES parser
  mappers.c           Mapper 0 (NROM)
  ppu.c               2C02 PPU — fetch pipeline, sprites, scrolling
  apu.c               2A03 APU — channels, frame counter, mixer
  timing.c            audio-sync throttle
  ringbuffer.c        lock-free audio ring buffer
  debug_window.c      SDL2_ttf debug overlay
testroms/         test ROMs (nestest, blargg tests, etc.)
testlog/          nestest.log for compare-mode validation
```
