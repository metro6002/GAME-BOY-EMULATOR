#include "gb.h"

#include <stdio.h>

static bool run_to_frame(GameBoy *gb)
{
    gb->frame_ready = false;
    for (unsigned step = 0; step < 200000; ++step) {
        uint8_t opcode;
        if (gb_step(gb, &opcode) != GB_STEP_OK) return false;
        if (gb->frame_ready && gb->io[0x44] == 144) return true;
        if (gb->frame_ready) gb->frame_ready = false;
    }
    return false;
}

int main(void)
{
    GameBoy gb = {0};
    if (!gb_load_rom(&gb, "demo.gb")) return 1;
    if (!run_to_frame(&gb)) {
        fputs("Demo did not reach its first frame\n", stderr);
        gb_free(&gb);
        return 1;
    }
    unsigned old_pixel = 72 * GB_SCREEN_WIDTH + 80;
    unsigned new_pixel = 72 * GB_SCREEN_WIDTH + 88;
    if (gb.pixels[old_pixel] != 0x00081820) {
        fprintf(stderr, "Demo did not draw its starting square: pixel=%08X "
                        "tile=%02X data=%02X LY=%u PC=%04X\n",
                gb.pixels[old_pixel], gb.vram[0x192A], gb.vram[16],
                gb.io[0x44], gb.cpu.pc);
        gb_free(&gb);
        return 1;
    }
    gb_set_button(&gb, 0, true); /* Right */
    if (!run_to_frame(&gb) ||
        gb.pixels[old_pixel] != 0x00E0F8D0 ||
        gb.pixels[new_pixel] != 0x00081820) {
        fputs("Demo did not move right in response to input\n", stderr);
        gb_free(&gb);
        return 1;
    }
    gb_free(&gb);
    puts("Demo ROM: frame rendered and Right key moved the square: OK");
    return 0;
}
