# Game Boy Emulator in C

![Language](https://img.shields.io/badge/Language-C11-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20(Win32)-lightgrey.svg)
![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

A lightweight, playable original Game Boy (DMG) emulator written in C11 for Windows. Designed as an educational project, it provides accurate CPU opcode execution, memory banking, PPU background/sprite rendering, frame pacing, and battery-backed cartridge RAM saves.

---

## Technical Overview & Features

- **CPU Core (`LR35902`)**:
  - Implements all 245 legal base instructions and 256 `CB`-prefixed opcodes.
  - Complete flag handling, 16-bit stack operations, hardware interrupts (V-Blank, LCD STAT, Timer, Serial, Joypad), EI instruction delay, HALT state, and HALT bug emulation.
  - Post-boot register state initialization at `0x0100` (runs commercial ROMs without requiring an external boot ROM).

- **Memory Bus & Cartridge Mappers**:
  - Full 16-bit memory map addressing VRAM, Work RAM (WRAM + Echo RAM), High RAM (HRAM), OAM, Joypad, Timers, and I/O registers.
  - Cartridge Mapper Support:
    - **ROM-Only** (up to 32 KB)
    - **MBC1** (Bank switching, RAM enable, banking modes)
    - **MBC3** (Without RTC)
    - **MBC5** (16-bit ROM banking)
  - Automatic persistent SRAM saving (`.sav` files created alongside the loaded ROM upon closing and auto-saved periodically every ~10 seconds).

- **Graphics (PPU)**:
  - 160×144 line-by-line rendering for Background, Window, and Sprites (8×8 and 8×16 tile modes).
  - Sprite horizontal/vertical flipping, transparency, background priority, and the hardware 10-sprites-per-line evaluation limit.
  - Native DMG palette mapping to 32-bit ARGB desktop surfaces.

- **Host Interface (Win32)**:
  - Native Windows GDI graphics pipeline with window resizing and `COLORONCOLOR` stretch mode.
  - Sub-millisecond frame pacing targeting the authentic 59.7 Hz Game Boy refresh rate using `QueryPerformanceCounter`.

---

## Controls

| Key | Game Boy Button |
| :--- | :--- |
| **Arrow Keys** (`Up`, `Down`, `Left`, `Right`) | D-Pad |
| **Z** | A Button |
| **X** | B Button |
| **Enter** | Start |
| **Shift** | Select |
| **Escape** | Close Emulator |

---

## Build and Play

### Prerequisites
- GCC (`gcc` with C11 support)
- `mingw32-make` (or standard GNU `make`)

### Building

To build the graphical player and test suite, run:

```powershell
# Build both main.exe and gbplay.exe
mingw32-make all

# Run the automated CPU and hardware self-test suite
mingw32-make test
```

### Running the Emulator

- **Play ROMs with GUI Player (`gbplay.exe`)**:
  ```powershell
  .\gbplay.exe path\to\game.gb
  ```
  *Saves battery RAM to `game.gb.sav` automatically on exit.*

- **Headless / Console Trace Runner (`main.exe`)**:
  ```powershell
  # Run built-in instruction demo
  .\main.exe

  # Run headless CPU execution diagnostic on a ROM
  .\main.exe path\to\game.gb
  ```

---

## Repository Structure

```
GAME-BOY-EMULATOR/
├── gb.h                # Primary header defining core hardware data structures & API
├── Makefile            # MinGW build script for targets (main.exe, gbplay.exe, selftest.exe)
├── src/
│   ├── gb_cpu.c        # LR35902 CPU instruction decoding, execution & registers
│   ├── gb_bus.c        # 16-bit memory bus dispatch, MBC1/3/5 mappers, timers, DMA & interrupts
│   ├── gb_ppu.c        # Line-based Pixel Processing Unit (Background, Window, Sprites)
│   ├── gb_win32.c      # Win32 desktop windowing, GDI surface rendering, input & save files
│   └── main.c          # Console test runner and built-in opcode validation program
└── tests/
    ├── selftest.c      # Integration test checking memory, opcodes, interrupts, MBCs & rendering
    └── memory_bus.c    # Unit testing suite for memory bus operations
```

---

## Hardware Limitations & Compatibility

- **Audio**: Sound (APU) is not currently implemented.
- **Timing Accuracy**: The PPU currently utilizes scanline-based line rendering rather than cycle-by-cycle pixel FIFO fetches, and OAM DMA transfers complete immediately rather than locking the bus for 160 microseconds.
- **Unsupported Hardware**: MBC2, MBC3 RTC (Real-Time Clock), Game Boy Color (CGB) extensions, Super Game Boy (SGB), link cable communication, and rumble motor features are not supported.

Monochrome DMG games using ROM-Only, MBC1, MBC3 (no RTC), or MBC5 mappers are the primary compatibility targets.

---

## References & Resources

- [Pan Docs](https://gbdev.io/pandocs/) — Comprehensive Game Boy Technical Reference
- [Cinoop](https://cturt.github.io/cinoop.html) — Staged approach to building a Game Boy emulator
- `GBCPUman.pdf` — Game Boy CPU manual (included in repository root)
