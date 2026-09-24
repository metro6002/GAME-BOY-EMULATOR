#include "gb.h"

static const uint32_t shades[4] = {
    0x00E0F8D0, 0x0088C070, 0x00346856, 0x00081820
};

void gb_clear_screen(GameBoy *gb)
{
    for (unsigned i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; ++i)
        gb->pixels[i] = shades[0];
}

static uint8_t tile_pixel(const GameBoy *gb, unsigned tile_offset,
                          unsigned x, unsigned y)
{
    unsigned row = tile_offset + (y & 7) * 2;
    unsigned bit = 7 - (x & 7);
    return (uint8_t)(((gb->vram[row] >> bit) & 1) |
                     (((gb->vram[row + 1] >> bit) & 1) << 1));
}

static uint8_t background_pixel(const GameBoy *gb, bool window,
                                unsigned x, unsigned y)
{
    uint8_t lcdc = gb->io[0x40];
    unsigned map = window ? ((lcdc & 0x40) ? 0x1C00 : 0x1800)
                          : ((lcdc & 0x08) ? 0x1C00 : 0x1800);
    unsigned map_index = map + ((y >> 3) & 31) * 32 + ((x >> 3) & 31);
    uint8_t tile = gb->vram[map_index];
    unsigned offset = (lcdc & 0x10) ? tile * 16
                      : (unsigned)(0x1000 + (int8_t)tile * 16);
    return tile_pixel(gb, offset, x, y);
}

/* OAM scan keeps the first ten sprites whose Y range crosses this line. */
static unsigned scan_sprites(const GameBoy *gb, unsigned line, unsigned selected[10])
{
    unsigned count = 0;
    unsigned height = (gb->io[0x40] & 4) ? 16 : 8;
    for (unsigned i = 0; i < 40 && count < 10; ++i) {
        int top = (int)gb->oam[i * 4] - 16;
        if ((int)line >= top && (int)line < top + (int)height)
            selected[count++] = i;
    }
    return count;
}

static bool sprite_pixel(const GameBoy *gb, const unsigned selected[10],
                         unsigned count, unsigned x, unsigned line,
                         uint8_t *color, uint8_t *attributes)
{
    int best_x = 256;
    unsigned best_index = 40;
    uint8_t best_color = 0;
    uint8_t best_attributes = 0;
    unsigned height = (gb->io[0x40] & 4) ? 16 : 8;
    for (unsigned i = 0; i < count; ++i) {
        unsigned index = selected[i];
        const uint8_t *oam = &gb->oam[index * 4];
        int left = (int)oam[1] - 8;
        if ((int)x < left || (int)x >= left + 8) continue;
        int row = (int)line - ((int)oam[0] - 16);
        unsigned column = (unsigned)((int)x - left);
        uint8_t flags = oam[3];
        if (flags & 0x40) row = (int)height - 1 - row;
        if (flags & 0x20) column = 7 - column;
        unsigned tile = oam[2];
        if (height == 16) tile &= 0xFE;
        if (row >= 8) { ++tile; row -= 8; }
        uint8_t pixel = tile_pixel(gb, tile * 16, column, (unsigned)row);
        if (!pixel) continue;
        if (left < best_x || (left == best_x && index < best_index)) {
            best_x = left;
            best_index = index;
            best_color = pixel;
            best_attributes = flags;
        }
    }
    if (best_index == 40) return false;
    *color = best_color;
    *attributes = best_attributes;
    return true;
}

void gb_render_line(GameBoy *gb, unsigned line)
{
    if (line >= GB_SCREEN_HEIGHT) return;
    uint8_t lcdc = gb->io[0x40];
    bool bg_enabled = (lcdc & 1) != 0;
    bool window_visible = bg_enabled && (lcdc & 0x20) &&
                          line >= gb->io[0x4A] && gb->io[0x4B] <= 166;
    unsigned selected[10];
    unsigned sprite_count = (lcdc & 2) ? scan_sprites(gb, line, selected) : 0;
    for (unsigned x = 0; x < GB_SCREEN_WIDTH; ++x) {
        uint8_t bg_color = 0;
        if (bg_enabled) {
            int window_x = (int)gb->io[0x4B] - 7;
            if (window_visible && (int)x >= window_x) {
                bg_color = background_pixel(gb, true,
                                            (unsigned)((int)x - window_x),
                                            gb->window_line);
            } else {
                bg_color = background_pixel(gb, false,
                                            (uint8_t)(x + gb->io[0x43]),
                                            (uint8_t)(line + gb->io[0x42]));
            }
        }
        unsigned shade = (gb->io[0x47] >> (bg_color * 2)) & 3;
        uint8_t obj_color, attributes;
        if (sprite_count && sprite_pixel(gb, selected, sprite_count, x, line,
                                         &obj_color, &attributes) &&
            (!(attributes & 0x80) || bg_color == 0)) {
            uint8_t palette = gb->io[(attributes & 0x10) ? 0x49 : 0x48];
            shade = (palette >> (obj_color * 2)) & 3;
        }
        gb->pixels[line * GB_SCREEN_WIDTH + x] = shades[shade];
    }
    if (window_visible) ++gb->window_line;
}
