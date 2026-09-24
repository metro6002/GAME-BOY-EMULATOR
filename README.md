# Game Boy emulator in C

A playable original Game Boy (DMG) emulator for Windows, built as a learning
project. The CPU manual in this folder and [Pan Docs](https://gbdev.io/pandocs/)
are the hardware references. [Cinoop](https://cturt.github.io/cinoop.html)
informed the staged approach to building and tracing the emulator.

## Build and play

Only MinGW GCC and `mingw32-make` are needed. From this directory:

```powershell
mingw32-make all test
.\gbplay.exe .\demo.gb
```

The included `demo.gb` is an original minimal game: move the square with the
arrow keys. To run another monochrome Game Boy cartridge:

```powershell
.\gbplay.exe path\to\game.gb
```

Controls: **arrows** = D-pad, **Z** = A, **X** = B, **Enter** = Start,
**Shift** = Select, **Esc** = close. The window can be resized. Battery-backed
cartridge RAM is saved next to the ROM as `game.gb.sav` on close and about
every ten seconds while playing.

`gbemu.exe` remains a console CPU trace demo. `demo_rom.c` builds `demo.gb`.

## Implemented

- All 245 legal base CPU opcodes and 256 CB-prefixed opcodes, flags, stack,
  interrupt dispatch, EI delay, and HALT behavior.
- 16-bit memory bus, ROM-only, MBC1, MBC3 without RTC, and MBC5 cartridges,
  cartridge RAM and saves,
  VRAM, work RAM and echo, OAM, high RAM, joypad, timer, serial transfer,
  interrupt registers, and OAM DMA copying.
- Background, window, and sprite rendering with DMG palettes, sprite flips,
  priority, and the ten-sprites-per-line limit.
- Native Windows window and keyboard controls with 59.7 Hz frame pacing.

The emulator starts at `0x0100` with approximate post-boot DMG register values;
it does not require a boot ROM. ROMs must have a supported cartridge header.

## Limits

Sound is not implemented. The PPU uses a fixed Mode 3 duration, and DMA
copies immediately instead of occupying the CPU bus for its full duration.
These timing differences may affect some games. MBC2, MBC3 RTC cartridges,
Color Game Boy features, rumble, and link-cable communication are not
supported. Games using those features are rejected or will not work correctly.
The included demo ROM and monochrome ROM-only, MBC1, MBC3 without RTC, or
MBC5 games are the current compatibility target.

The focused tests check opcode decoding, selected instruction behavior,
memory mapping, timers, interrupts, MBC1/3/5, rendering, and a full two-frame
input-to-pixel path through `demo.gb`. They do not prove compatibility with
every commercial game.

## Source map

- `gb_cpu.c`: CPU instructions and registers.
- `gb_bus.c`: memory devices, cartridge, timer, serial, DMA, and LCD timing.
- `gb_ppu.c`: background, window, and sprite pixels.
- `gb_win32.c`: player window, input, pacing, and save files.
- `gbemu.c`: console trace runner.
- `selftest.c`, `playtest.c`: focused and integration checks.
