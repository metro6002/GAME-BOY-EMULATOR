#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t declared_rom_size(uint8_t code)
{
    if (code <= 8) return (size_t)0x8000 << code;
    return 0;
}

static size_t declared_ram_size(uint8_t code)
{
    if (code == 0) return 0;
    if (code == 2) return 0x2000;
    if (code == 3) return 0x8000;
    if (code == 4) return 0x20000;
    if (code == 5) return 0x10000;
    return 0;
}

bool gb_load_rom(GameBoy *gb, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        perror(path);
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0 || (unsigned long)length > 0x800000UL ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Unsupported ROM file size: %s\n", path);
        fclose(file);
        return false;
    }
    if (length < 0x8000) {
        fprintf(stderr, "ROM is shorter than 32 KiB: %s\n", path);
        fclose(file);
        return false;
    }
    uint8_t *rom = malloc((size_t)length);
    if (!rom) {
        fputs("Out of memory loading ROM\n", stderr);
        fclose(file);
        return false;
    }
    bool read_ok = fread(rom, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!read_ok) {
        fprintf(stderr, "Could not read ROM: %s\n", path);
        free(rom);
        return false;
    }

    uint8_t type = rom[0x147];
    uint8_t ram_code = rom[0x149];
    size_t expected = declared_rom_size(rom[0x148]);
    size_t ram_size = declared_ram_size(ram_code);
    bool rom_only = type == 0x00 || type == 0x08 || type == 0x09;
    bool mbc1 = type >= 0x01 && type <= 0x03;
    bool mbc3 = type >= 0x11 && type <= 0x13;
    bool mbc5 = type >= 0x19 && type <= 0x1B;
    if (!expected || expected != (size_t)length ||
        (!rom_only && !mbc1 && !mbc3 && !mbc5) ||
        (ram_code != 0 && ram_size == 0) ||
        (rom_only && (expected != 0x8000 || ram_size > 0x2000)) ||
        (mbc1 && (expected > 0x200000 ||
                  (expected > 0x80000 && ram_size > 0x2000) ||
                  ram_size > 0x8000)) ||
        (mbc3 && (expected > 0x200000 || ram_size > 0x8000))) {
        fprintf(stderr, "Unsupported cartridge type or header size: %s\n", path);
        free(rom);
        return false;
    }
    uint8_t *ram = ram_size ? calloc(ram_size, 1) : NULL;
    if (ram_size && !ram) {
        fputs("Out of memory loading cartridge RAM\n", stderr);
        free(rom);
        return false;
    }
    gb_free(gb);
    gb->cart.rom = rom;
    gb->cart.rom_size = (size_t)length;
    gb->cart.ram = ram;
    gb->cart.ram_size = ram_size;
    gb->cart.mapper = mbc1 ? GB_MBC1 : mbc3 ? GB_MBC3 :
                      mbc5 ? GB_MBC5 : GB_ROM_ONLY;
    gb->cart.battery = type == 0x03 || type == 0x09 ||
                       type == 0x13 || type == 0x1B;
    gb_reset(gb);
    return true;
}

void gb_free(GameBoy *gb)
{
    free(gb->cart.rom);
    free(gb->cart.ram);
    gb->cart.rom = NULL;
    gb->cart.ram = NULL;
    gb->cart.rom_size = 0;
    gb->cart.ram_size = 0;
}

void gb_reset(GameBoy *gb)
{
    memset(&gb->cpu, 0, sizeof gb->cpu);
    memset(gb->vram, 0, sizeof gb->vram);
    memset(gb->wram, 0, sizeof gb->wram);
    memset(gb->oam, 0, sizeof gb->oam);
    memset(gb->io, 0, sizeof gb->io);
    memset(gb->hram, 0, sizeof gb->hram);
    gb->cart.rom_bank_low = 1;
    gb->cart.bank_high = 0;
    gb->cart.banking_mode = 0;
    gb->cart.rom_bank_number = 1;
    gb->cart.ram_bank_number = 0;
    gb->cart.ram_enabled = false;
    gb->cart.ram_dirty = false;
    gb->cpu.a = 0x01;
    gb->cpu.f = 0xB0;
    gb->cpu.c = 0x13;
    gb->cpu.e = 0xD8;
    gb->cpu.h = 0x01;
    gb->cpu.l = 0x4D;
    gb->cpu.sp = 0xFFFE;
    gb->cpu.pc = 0x0100; /* Boot ROM is skipped for now. */
    gb->io[0x00] = 0xCF;
    gb->io[0x40] = 0x91;
    gb->io[0x41] = 0x82;
    gb->io[0x47] = 0xFC;
    gb->io[0x48] = 0xFF;
    gb->io[0x49] = 0xFF;
    gb->ie = 0;
    gb->div_counter = 0;
    gb->timer_reload_delay = 0;
    gb->serial_ticks = 0;
    gb->serial_bits = 0;
    gb->ppu_dot = 0;
    gb->window_line = 0;
    gb->stat_line = false;
    gb->buttons = 0;
    gb->frame_ready = false;
    gb_clear_screen(gb);
}

static size_t rom_bank(const GameBoy *gb, bool upper)
{
    const GbCartridge *cart = &gb->cart;
    if (cart->mapper == GB_ROM_ONLY) return upper ? 1 : 0;
    if (cart->mapper == GB_MBC3)
        return upper ? (cart->rom_bank_number ? cart->rom_bank_number : 1) : 0;
    if (cart->mapper == GB_MBC5)
        return upper ? cart->rom_bank_number : 0;
    size_t low = cart->rom_bank_low & 0x1F;
    if (low == 0) low = 1;
    bool large = cart->rom_size > 0x80000;
    size_t high = large ? ((size_t)(cart->bank_high & 3) << 5) : 0;
    if (upper) return high | low;
    return cart->banking_mode && large ? high : 0;
}

static size_t ram_index(const GameBoy *gb, uint16_t address)
{
    size_t bank = 0;
    if (gb->cart.mapper == GB_MBC3 || gb->cart.mapper == GB_MBC5)
        bank = gb->cart.ram_bank_number;
    else if (gb->cart.mapper == GB_MBC1 && gb->cart.banking_mode &&
        gb->cart.rom_size <= 0x80000)
        bank = gb->cart.bank_high & 3;
    return (bank * 0x2000 + (address - 0xA000)) % gb->cart.ram_size;
}

static uint8_t joypad(const GameBoy *gb)
{
    uint8_t result = (uint8_t)(0xC0 | (gb->io[0] & 0x30) | 0x0F);
    if ((result & 0x10) == 0) result &= (uint8_t)~(gb->buttons & 0x0F);
    if ((result & 0x20) == 0) result &= (uint8_t)~((gb->buttons >> 4) & 0x0F);
    return result;
}

void gb_set_button(GameBoy *gb, unsigned button, bool pressed)
{
    if (button >= 8) return;
    uint8_t before = joypad(gb);
    if (pressed) gb->buttons |= (uint8_t)(1u << button);
    else gb->buttons &= (uint8_t)~(1u << button);
    if (pressed) gb->cpu.stopped = false;
    if ((before & (uint8_t)~joypad(gb) & 0x0F) != 0)
        gb->io[0x0F] |= 0x10;
}

static bool timer_signal(const GameBoy *gb)
{
    static const unsigned bits[4] = {9, 3, 5, 7};
    uint8_t tac = gb->io[0x07];
    return (tac & 4) && ((gb->div_counter >> bits[tac & 3]) & 1);
}

static void timer_increment(GameBoy *gb)
{
    if (gb->timer_reload_delay) return;
    if (gb->io[0x05] == 0xFF) {
        gb->io[0x05] = 0;
        gb->timer_reload_delay = 4;
    } else {
        ++gb->io[0x05];
    }
}

static unsigned ppu_mode(const GameBoy *gb)
{
    if (!(gb->io[0x40] & 0x80)) return 0;
    if (gb->io[0x44] >= 144) return 1;
    if (gb->ppu_dot < 80) return 2;
    if (gb->ppu_dot < 252) return 3;
    return 0;
}

static void update_stat(GameBoy *gb)
{
    uint8_t stat = gb->io[0x41];
    bool equal = gb->io[0x44] == gb->io[0x45];
    unsigned mode = ppu_mode(gb);
    stat = (uint8_t)((stat & 0x78) | 0x80 | (equal ? 4 : 0) | mode);
    gb->io[0x41] = stat;
    bool line = (gb->io[0x40] & 0x80) &&
        ((equal && (stat & 0x40)) ||
         (mode == 0 && (stat & 0x08)) ||
         (mode == 1 && (stat & 0x10)) ||
         (mode == 2 && (stat & 0x20)));
    if (line && !gb->stat_line) gb->io[0x0F] |= 0x02;
    gb->stat_line = line;
}

void gb_tick(GameBoy *gb, unsigned tcycles)
{
    for (unsigned i = 0; i < tcycles; ++i) {
        if (gb->serial_ticks && --gb->serial_ticks == 0) {
            gb->io[0x01] = (uint8_t)((gb->io[0x01] << 1) | 1);
            if (++gb->serial_bits == 8) {
                gb->io[0x02] &= (uint8_t)~0x80;
                gb->io[0x0F] |= 0x08;
            } else {
                gb->serial_ticks = 512;
            }
        }
        if (gb->timer_reload_delay && --gb->timer_reload_delay == 0) {
            gb->io[0x05] = gb->io[0x06];
            gb->io[0x0F] |= 0x04;
        }
        bool old_signal = timer_signal(gb);
        ++gb->div_counter;
        if (old_signal && !timer_signal(gb)) timer_increment(gb);
        if (gb->io[0x40] & 0x80) {
            if (gb->ppu_dot == 252 && gb->io[0x44] < 144)
                gb_render_line(gb, gb->io[0x44]);
            if (++gb->ppu_dot == 456) {
                gb->ppu_dot = 0;
                if (++gb->io[0x44] == 144) {
                    gb->io[0x0F] |= 0x01;
                    gb->frame_ready = true;
                }
                if (gb->io[0x44] == 154) {
                    gb->io[0x44] = 0;
                    gb->window_line = 0;
                }
            }
            update_stat(gb);
        }
    }
    gb->cpu.cycles += tcycles;
}

uint8_t gb_read(GameBoy *gb, uint16_t address)
{
    if (address < 0x8000) {
        if (!gb->cart.rom) return 0xFF;
        size_t bank_count = gb->cart.rom_size / 0x4000;
        size_t offset = (rom_bank(gb, address >= 0x4000) % bank_count) * 0x4000 +
                        (address & 0x3FFF);
        return offset < gb->cart.rom_size ? gb->cart.rom[offset] : 0xFF;
    }
    if (address < 0xA000) {
        return ppu_mode(gb) == 3 ? 0xFF : gb->vram[address - 0x8000];
    }
    if (address < 0xC000) {
        if (!gb->cart.ram ||
            (gb->cart.mapper != GB_ROM_ONLY && !gb->cart.ram_enabled) ||
            (gb->cart.mapper == GB_MBC3 && gb->cart.ram_bank_number > 3))
            return 0xFF;
        return gb->cart.ram[ram_index(gb, address)];
    }
    if (address < 0xE000) return gb->wram[address - 0xC000];
    if (address < 0xFE00) return gb->wram[address - 0xE000];
    if (address < 0xFEA0) {
        return ppu_mode(gb) >= 2 ? 0xFF : gb->oam[address - 0xFE00];
    }
    if (address < 0xFF00) return 0xFF;
    if (address < 0xFF80) {
        unsigned index = address - 0xFF00;
        if (index == 0) return joypad(gb);
        if (index == 2) return (uint8_t)(gb->io[index] | 0x7E);
        if (index == 4) return (uint8_t)(gb->div_counter >> 8);
        if (index == 7) return (uint8_t)(gb->io[index] | 0xF8);
        if (index == 0x0F) return (uint8_t)(gb->io[index] | 0xE0);
        return gb->io[index];
    }
    if (address < 0xFFFF) return gb->hram[address - 0xFF80];
    return (uint8_t)(gb->ie | 0xE0);
}

void gb_write(GameBoy *gb, uint16_t address, uint8_t value)
{
    if (address < 0x8000) {
        if (gb->cart.mapper == GB_ROM_ONLY) return;
        if (address < 0x2000) {
            gb->cart.ram_enabled = (value & 0x0F) == 0x0A;
        } else if (gb->cart.mapper == GB_MBC1) {
            if (address < 0x4000) gb->cart.rom_bank_low = value & 0x1F;
            else if (address < 0x6000) gb->cart.bank_high = value & 3;
            else gb->cart.banking_mode = value & 1;
        } else if (gb->cart.mapper == GB_MBC3) {
            if (address < 0x4000) {
                gb->cart.rom_bank_number = value & 0x7F;
            } else if (address < 0x6000) {
                gb->cart.ram_bank_number = value;
            }
        } else if (gb->cart.mapper == GB_MBC5) {
            if (address < 0x3000)
                gb->cart.rom_bank_number =
                    (uint16_t)((gb->cart.rom_bank_number & 0x100) | value);
            else if (address < 0x4000)
                gb->cart.rom_bank_number =
                    (uint16_t)((gb->cart.rom_bank_number & 0xFF) |
                               ((value & 1) << 8));
            else if (address < 0x6000)
                gb->cart.ram_bank_number = value & 0x0F;
        }
        return;
    }
    if (address < 0xA000) {
        if (ppu_mode(gb) != 3) gb->vram[address - 0x8000] = value;
        return;
    }
    if (address < 0xC000) {
        if (gb->cart.ram &&
            (gb->cart.mapper == GB_ROM_ONLY || gb->cart.ram_enabled) &&
            (gb->cart.mapper != GB_MBC3 || gb->cart.ram_bank_number <= 3)) {
            size_t index = ram_index(gb, address);
            if (gb->cart.ram[index] != value) {
                gb->cart.ram[index] = value;
                gb->cart.ram_dirty = true;
            }
        }
        return;
    }
    if (address < 0xE000) { gb->wram[address - 0xC000] = value; return; }
    if (address < 0xFE00) { gb->wram[address - 0xE000] = value; return; }
    if (address < 0xFEA0) {
        if (ppu_mode(gb) < 2) gb->oam[address - 0xFE00] = value;
        return;
    }
    if (address < 0xFF00) return;
    if (address < 0xFF80) {
        unsigned index = address - 0xFF00;
        if (index == 0) {
            uint8_t before = joypad(gb);
            gb->io[0] = value & 0x30;
            if ((before & (uint8_t)~joypad(gb) & 0x0F) != 0)
                gb->io[0x0F] |= 0x10;
            return;
        }
        if (index == 2) {
            gb->io[index] = value & 0x81;
            gb->serial_bits = 0;
            gb->serial_ticks = (value & 0x81) == 0x81 ? 512 : 0;
            return;
        }
        if (index == 4 || index == 7) {
            bool old_signal = timer_signal(gb);
            if (index == 4) gb->div_counter = 0;
            else gb->io[7] = value & 7;
            if (old_signal && !timer_signal(gb)) timer_increment(gb);
            return;
        }
        if (index == 5) {
            gb->timer_reload_delay = 0;
            gb->io[index] = value;
            return;
        }
        if (index == 0x0F) { gb->io[index] = value & 0x1F; return; }
        if (index == 0x41) {
            gb->io[index] = (uint8_t)((gb->io[index] & 0x07) | (value & 0x78));
            update_stat(gb);
            return;
        }
        if (index == 0x44) return;
        if (index == 0x45) {
            gb->io[index] = value;
            update_stat(gb);
            return;
        }
        if (index == 0x40) {
            bool was_on = (gb->io[index] & 0x80) != 0;
            gb->io[index] = value;
            if (was_on && !(value & 0x80)) {
                gb->ppu_dot = 0;
                gb->io[0x44] = 0;
                gb->window_line = 0;
                gb_clear_screen(gb);
                gb->frame_ready = true;
            }
            update_stat(gb);
            return;
        }
        if (index == 0x46) {
            gb->io[index] = value;
            for (unsigned i = 0; i < 0xA0; ++i)
                gb->oam[i] = gb_read(gb, (uint16_t)((value << 8) | i));
            return;
        }
        gb->io[index] = value;
        return;
    }
    if (address < 0xFFFF) { gb->hram[address - 0xFF80] = value; return; }
    gb->ie = value & 0x1F;
}
