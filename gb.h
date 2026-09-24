#ifndef GB_H
#define GB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { GB_SCREEN_WIDTH = 160, GB_SCREEN_HEIGHT = 144 };

typedef enum { GB_ROM_ONLY, GB_MBC1, GB_MBC3, GB_MBC5 } GbMapper;

typedef struct {
    uint8_t *rom;
    size_t rom_size;
    uint8_t *ram;
    size_t ram_size;
    GbMapper mapper;
    uint8_t rom_bank_low;
    uint8_t bank_high;
    uint8_t banking_mode;
    uint16_t rom_bank_number;
    uint8_t ram_bank_number;
    bool ram_enabled;
    bool battery;
    bool ram_dirty;
} GbCartridge;

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t pc, sp;
    bool ime, halted, stopped, halt_bug;
    unsigned ime_delay;
    uint64_t cycles;
} GbCpu;

typedef struct {
    GbCartridge cart;
    GbCpu cpu;
    uint8_t vram[0x2000];
    uint8_t wram[0x2000];
    uint8_t oam[0xA0];
    uint8_t io[0x80];
    uint8_t hram[0x7F];
    uint8_t ie;
    uint16_t div_counter;
    unsigned timer_reload_delay;
    unsigned serial_ticks;
    unsigned serial_bits;
    unsigned ppu_dot;
    unsigned window_line;
    bool stat_line;
    uint8_t buttons; /* 1 bit per pressed button; bit 0=Right/A, ... */
    uint32_t pixels[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
    bool frame_ready;
} GameBoy;

typedef enum { GB_STEP_OK, GB_STEP_ILLEGAL, GB_STEP_STOPPED } GbStepResult;

bool gb_load_rom(GameBoy *gb, const char *path);
void gb_free(GameBoy *gb);
void gb_reset(GameBoy *gb);
uint8_t gb_read(GameBoy *gb, uint16_t address);
void gb_write(GameBoy *gb, uint16_t address, uint8_t value);
void gb_tick(GameBoy *gb, unsigned tcycles);
GbStepResult gb_step(GameBoy *gb, uint8_t *opcode);
void gb_set_button(GameBoy *gb, unsigned button, bool pressed);
void gb_render_line(GameBoy *gb, unsigned line);
void gb_clear_screen(GameBoy *gb);

#endif
