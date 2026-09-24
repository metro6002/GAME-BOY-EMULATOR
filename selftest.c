#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static bool illegal_opcode(unsigned opcode)
{
    static const uint8_t illegal[] = {
        0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB,
        0xEC, 0xED, 0xF4, 0xFC, 0xFD
    };
    for (size_t i = 0; i < sizeof illegal; ++i)
        if (opcode == illegal[i]) return true;
    return false;
}

int main(void)
{
    GameBoy gb = {0};
    uint8_t rom[0x8000] = {0};
    gb.cart.rom = rom;
    gb.cart.rom_size = sizeof rom;
    gb.cart.mapper = GB_ROM_ONLY;
    gb_reset(&gb);

    gb_write(&gb, 0xC123, 0x42);
    CHECK(gb_read(&gb, 0xE123) == 0x42);
    gb_write(&gb, 0xE124, 0x99);
    CHECK(gb_read(&gb, 0xC124) == 0x99);
    CHECK(gb_read(&gb, 0xFEA0) == 0xFF);
    gb_write(&gb, 0xFF40, 0); /* Turn off LCD so CPU can read OAM. */
    gb_write(&gb, 0xFF46, 0xC1);
    CHECK(gb_read(&gb, 0xFE00 + 0x23) == 0x42);
    gb_write(&gb, 0xFF07, 0x05); /* Timer: divider bit 3. */
    gb_tick(&gb, 16);
    CHECK(gb_read(&gb, 0xFF05) == 1);
    gb_write(&gb, 0xFF04, 0xAB);
    CHECK(gb_read(&gb, 0xFF04) == 0);
    gb_write(&gb, 0xFF05, 0xFF);
    gb_write(&gb, 0xFF06, 0xAB);
    gb_tick(&gb, 16);
    CHECK(gb_read(&gb, 0xFF05) == 0);
    CHECK((gb_read(&gb, 0xFF0F) & 4) == 0);
    gb_tick(&gb, 4);
    CHECK(gb_read(&gb, 0xFF05) == 0xAB);
    CHECK((gb_read(&gb, 0xFF0F) & 4) != 0);
    gb_write(&gb, 0xFF07, 0);
    gb_write(&gb, 0xFF01, 0xA5);
    gb_write(&gb, 0xFF02, 0x81);
    gb_tick(&gb, 4095);
    CHECK((gb_read(&gb, 0xFF0F) & 8) == 0);
    gb_tick(&gb, 1);
    CHECK(gb_read(&gb, 0xFF01) == 0xFF);
    CHECK((gb_read(&gb, 0xFF02) & 0x80) == 0);
    CHECK((gb_read(&gb, 0xFF0F) & 8) != 0);
    gb_write(&gb, 0xFF40, 0x91);
    gb_tick(&gb, 456u * 144u);
    CHECK(gb_read(&gb, 0xFF44) == 144);
    CHECK((gb_read(&gb, 0xFF0F) & 1) != 0);

    gb_reset(&gb);
    gb.io[0x40] = 0x93; /* LCD, background, and objects enabled. */
    gb.io[0x47] = 0xE4; /* Identity background palette. */
    gb.io[0x48] = 0xFC; /* Object color 1 maps to darkest shade. */
    gb.vram[0] = 0x80;  /* Background tile 0: color 1 at x=0. */
    gb.vram[16] = 0x80; /* Object tile 1: color 1 at x=0. */
    gb.oam[0] = 16; gb.oam[1] = 8; gb.oam[2] = 1;
    gb_tick(&gb, 253);
    CHECK(gb.pixels[0] == 0x00081820); /* Object is in front. */
    gb.oam[3] = 0x80;
    gb_render_line(&gb, 0);
    CHECK(gb.pixels[0] == 0x0088C070); /* Object is behind nonzero BG. */
    gb.io[0x40] = 0xF1; /* Enable window map 9C00; disable objects. */
    gb.io[0x4A] = 0; gb.io[0x4B] = 7;
    gb.vram[0x1C00] = 2;
    gb.vram[32 + 1] = 0x80; /* Window tile 2, color 2 at x=0. */
    gb.window_line = 0;
    gb_render_line(&gb, 0);
    CHECK(gb.pixels[0] == 0x00346856);
    CHECK(gb.window_line == 1);

    for (unsigned op = 0; op < 256; ++op) {
        memset(rom, 0, sizeof rom);
        gb_reset(&gb);
        rom[0x100] = (uint8_t)op;
        uint8_t executed;
        GbStepResult result = gb_step(&gb, &executed);
        CHECK(executed == op);
        CHECK((result == GB_STEP_ILLEGAL) == illegal_opcode(op));
    }
    for (unsigned op = 0; op < 256; ++op) {
        memset(rom, 0, sizeof rom);
        gb_reset(&gb);
        rom[0x100] = 0xCB;
        rom[0x101] = (uint8_t)op;
        uint8_t executed;
        CHECK(gb_step(&gb, &executed) == GB_STEP_OK);
        CHECK(executed == 0xCB);
    }

    memset(rom, 0, sizeof rom);
    gb_reset(&gb);
    rom[0x100] = 0xFB; /* EI has a one-instruction delay. */
    rom[0x101] = 0x00; /* NOP */
    gb.ie = 1;
    gb.io[0x0F] = 1;
    uint8_t executed;
    CHECK(gb_step(&gb, &executed) == GB_STEP_OK && !gb.cpu.ime);
    CHECK(gb_step(&gb, &executed) == GB_STEP_OK && gb.cpu.ime);
    CHECK(gb_step(&gb, &executed) == GB_STEP_OK);
    CHECK(gb.cpu.pc == 0x40 && gb.cpu.sp == 0xFFFC);
    CHECK(gb_read(&gb, 0xFFFC) == 0x02);
    CHECK(gb_read(&gb, 0xFFFD) == 0x01);

    memset(rom, 0, sizeof rom);
    gb_reset(&gb);
    static const uint8_t arithmetic_program[] = {
        0x3E, 0x09,       /* LD A,09 */
        0xC6, 0x01,       /* ADD A,01 */
        0x27,             /* DAA -> 10 */
        0xFE, 0x10,       /* CP 10 -> Z */
        0x28, 0x02,       /* JR Z,+2 */
        0x3E, 0xFF,       /* skipped */
        0xCD, 0x20, 0x01, /* CALL 0120 */
        0x76              /* HALT */
    };
    memcpy(&rom[0x100], arithmetic_program, sizeof arithmetic_program);
    rom[0x120] = 0x3C; /* INC A */
    rom[0x121] = 0xC9; /* RET */
    for (unsigned i = 0; i < 9; ++i)
        CHECK(gb_step(&gb, &executed) == GB_STEP_OK);
    CHECK(gb.cpu.halted && gb.cpu.a == 0x11 && gb.cpu.sp == 0xFFFE);
    CHECK(gb.cpu.pc == 0x010F && gb.cpu.cycles == 88);

    GameBoy mbc = {0};
    mbc.cart.rom_size = 0x100000;
    mbc.cart.rom = malloc(mbc.cart.rom_size);
    CHECK(mbc.cart.rom != NULL);
    mbc.cart.mapper = GB_MBC1;
    for (unsigned bank = 0; bank < 64; ++bank)
        memset(mbc.cart.rom + bank * 0x4000, (int)bank, 0x4000);
    gb_reset(&mbc);
    CHECK(gb_read(&mbc, 0x4000) == 1);
    gb_write(&mbc, 0x2000, 2);
    CHECK(gb_read(&mbc, 0x4000) == 2);
    gb_write(&mbc, 0x4000, 1);
    CHECK(gb_read(&mbc, 0x4000) == 34);
    gb_write(&mbc, 0x6000, 1);
    CHECK(gb_read(&mbc, 0x0000) == 32);
    mbc.cart.ram = calloc(0x8000, 1);
    CHECK(mbc.cart.ram != NULL);
    mbc.cart.ram_size = 0x8000;
    mbc.cart.rom_size = 0x80000; /* Small MBC1 ROM: high bits select RAM bank. */
    gb_reset(&mbc);
    gb_write(&mbc, 0x0000, 0x0A);
    gb_write(&mbc, 0x6000, 1);
    gb_write(&mbc, 0x4000, 2);
    gb_write(&mbc, 0xA123, 0x5A);
    gb_write(&mbc, 0x4000, 0);
    CHECK(gb_read(&mbc, 0xA123) == 0);
    gb_write(&mbc, 0x4000, 2);
    CHECK(gb_read(&mbc, 0xA123) == 0x5A);
    gb_write(&mbc, 0x0000, 0);
    CHECK(gb_read(&mbc, 0xA123) == 0xFF);
    gb_free(&mbc);

    GameBoy mbc3 = {0};
    mbc3.cart.rom_size = 0x200000;
    mbc3.cart.rom = calloc(mbc3.cart.rom_size, 1);
    mbc3.cart.ram_size = 0x8000;
    mbc3.cart.ram = calloc(mbc3.cart.ram_size, 1);
    CHECK(mbc3.cart.rom && mbc3.cart.ram);
    mbc3.cart.mapper = GB_MBC3;
    mbc3.cart.rom[32 * 0x4000] = 0x32;
    gb_reset(&mbc3);
    gb_write(&mbc3, 0x2000, 32);
    CHECK(gb_read(&mbc3, 0x4000) == 0x32);
    gb_write(&mbc3, 0x0000, 0x0A);
    gb_write(&mbc3, 0x4000, 2);
    gb_write(&mbc3, 0xA000, 0x75);
    gb_write(&mbc3, 0x4000, 0);
    CHECK(gb_read(&mbc3, 0xA000) == 0);
    gb_write(&mbc3, 0x4000, 2);
    CHECK(gb_read(&mbc3, 0xA000) == 0x75);
    gb_write(&mbc3, 0x4000, 8);
    CHECK(gb_read(&mbc3, 0xA000) == 0xFF);
    gb_free(&mbc3);

    GameBoy mbc5 = {0};
    mbc5.cart.rom_size = 0x800000;
    mbc5.cart.rom = calloc(mbc5.cart.rom_size, 1);
    mbc5.cart.ram_size = 0x20000;
    mbc5.cart.ram = calloc(mbc5.cart.ram_size, 1);
    CHECK(mbc5.cart.rom && mbc5.cart.ram);
    mbc5.cart.mapper = GB_MBC5;
    mbc5.cart.rom[258 * 0x4000] = 0xA5;
    gb_reset(&mbc5);
    gb_write(&mbc5, 0x2000, 2);
    gb_write(&mbc5, 0x3000, 1);
    CHECK(gb_read(&mbc5, 0x4000) == 0xA5);
    gb_write(&mbc5, 0x0000, 0x0A);
    gb_write(&mbc5, 0x4000, 15);
    gb_write(&mbc5, 0xA000, 0xC3);
    gb_write(&mbc5, 0x4000, 0);
    CHECK(gb_read(&mbc5, 0xA000) == 0);
    gb_write(&mbc5, 0x4000, 15);
    CHECK(gb_read(&mbc5, 0xA000) == 0xC3);
    gb_free(&mbc5);

    puts("Memory, timer, serial, interrupts, MBC1/3/5, 245 base and 256 CB opcodes: OK");
    return 0;
}
