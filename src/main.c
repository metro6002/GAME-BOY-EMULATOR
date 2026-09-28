#include "gb.h"

#include <stdio.h>
#include <stdlib.h>

static bool prepare_demo(GameBoy *gb){

    static const uint8_t program[] = {
        0x31, 0xFE, 0xFF,       /* LD SP,FFFE */
        0x21, 0x23, 0xC1,       /* LD HL,C123 */
        0x36, 0x42,             /* LD (HL),42 */
        0xFA, 0x23, 0xE1,       /* LD A,(E123): read the RAM echo */
        0xCB, 0x37,             /* SWAP A: demonstrate CB instruction */
        0xCB, 0x37,             /* SWAP A again, restoring 42 */
        0x76                    /* HALT */
    };
    gb->cart.rom = calloc(0x8000, 1);
    if (!gb->cart.rom) return false;
    gb->cart.rom_size = 0x8000;
    gb->cart.mapper = GB_ROM_ONLY;
    gb_reset(gb);
    for (size_t i = 0; i < sizeof program; ++i)
        gb->cart.rom[0x100 + i] = program[i];
    return true;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [cartridge.gb]\n", argv[0]);
        return 2;
    }
    GameBoy gb = {0};
    bool demo = argc == 1;
    if (demo ? !prepare_demo(&gb) : !gb_load_rom(&gb, argv[1])) {
        gb_free(&gb);
        return 1;
    }
    int status = 0;
    const unsigned limit = demo ? 100 : 1000000;
    for (unsigned i = 0; i < limit; ++i) {
        uint16_t pc = gb.cpu.pc;
        uint8_t opcode;
        GbStepResult result = gb_step(&gb, &opcode);
        if (demo)
            printf("%06u PC=%04X OP=%02X A=%02X F=%02X HL=%02X%02X "
                   "SP=%04X cycles=%llu\n", i, pc, opcode,
                   gb.cpu.a, gb.cpu.f, gb.cpu.h, gb.cpu.l, gb.cpu.sp,
                   (unsigned long long)gb.cpu.cycles);
        if (result == GB_STEP_ILLEGAL) {
            fprintf(stderr, "Illegal opcode %02X at PC=%04X\n", opcode, pc);
            status = 1;
            break;
        }
        if (result == GB_STEP_STOPPED) {
            fprintf(stderr, "CPU entered STOP at PC=%04X\n", pc);
            break;
        }
        if (gb.cpu.halted && gb.ie == 0) {
            if (demo && (gb.cpu.a != 0x42 || gb.wram[0x123] != 0x42)) {
                fputs("Demo state is incorrect\n", stderr);
                status = 1;
            }
            break;
        }
    }
    printf("Final: PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X "
           "HL=%02X%02X SP=%04X cycles=%llu\n",
           gb.cpu.pc, gb.cpu.a, gb.cpu.f, gb.cpu.b, gb.cpu.c,
           gb.cpu.d, gb.cpu.e, gb.cpu.h, gb.cpu.l, gb.cpu.sp,
           (unsigned long long)gb.cpu.cycles);
    gb_free(&gb);
    return status;
}
