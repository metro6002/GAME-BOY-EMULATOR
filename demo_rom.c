/* Build an original 32 KiB ROM: move a square using the D-pad. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { ROM_SIZE = 0x8000, MAX_FIXUPS = 16 };
enum Label {
    COPY_TILE, MAIN_LOOP, WAIT_VBLANK, NO_RIGHT, NO_LEFT,
    NO_UP, NO_DOWN, WAIT_LEAVE, LABEL_COUNT
};

typedef struct {
    enum Label target;
    size_t address;
    bool relative;
} Fixup;

typedef struct {
    uint8_t rom[ROM_SIZE];
    size_t pc;
    size_t labels[LABEL_COUNT];
    bool defined[LABEL_COUNT];
    Fixup fixups[MAX_FIXUPS];
    size_t fixup_count;
    bool failed;
} Assembler;

static void emit(Assembler *a, const uint8_t *bytes, size_t count)
{
    if (a->pc + count > ROM_SIZE) { a->failed = true; return; }
    memcpy(&a->rom[a->pc], bytes, count);
    a->pc += count;
}

#define EMIT(...) do { \
    const uint8_t bytes[] = {__VA_ARGS__}; \
    emit(&a, bytes, sizeof bytes); \
} while (0)

static void mark(Assembler *a, enum Label name)
{
    a->labels[name] = a->pc;
    a->defined[name] = true;
}

static void fix(Assembler *a, enum Label target, bool relative, size_t address)
{
    if (a->fixup_count >= MAX_FIXUPS) { a->failed = true; return; }
    a->fixups[a->fixup_count++] = (Fixup){target, address, relative};
}

static void jr(Assembler *a, uint8_t opcode, enum Label target)
{
    uint8_t bytes[] = {opcode, 0};
    emit(a, bytes, sizeof bytes);
    if (!a->failed) fix(a, target, true, a->pc - 1);
}

static void jp(Assembler *a, enum Label target)
{
    uint8_t bytes[] = {0xC3, 0, 0};
    emit(a, bytes, sizeof bytes);
    if (!a->failed) fix(a, target, false, a->pc - 2);
}

static bool resolve(Assembler *a)
{
    if (a->failed) return false;
    for (size_t i = 0; i < a->fixup_count; ++i) {
        Fixup fixup = a->fixups[i];
        if (!a->defined[fixup.target]) return false;
        size_t target = a->labels[fixup.target];
        if (fixup.relative) {
            long offset = (long)target - (long)(fixup.address + 1);
            if (offset < -128 || offset > 127) return false;
            a->rom[fixup.address] = (uint8_t)offset;
        } else {
            a->rom[fixup.address] = (uint8_t)target;
            a->rom[fixup.address + 1] = (uint8_t)(target >> 8);
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [output.gb]\n", argv[0]);
        return 2;
    }
    const char *output_name = argc == 2 ? argv[1] : "demo.gb";
    Assembler a = {0};
    a.pc = 0x150;
    const uint8_t entry[] = {0xC3, 0x50, 0x01}; /* JP 0150 */
    memcpy(&a.rom[0x100], entry, sizeof entry);
    memcpy(&a.rom[0x134], "SQUAREMOVE", 10);
    /* Header bytes 0147-0149 remain zero: ROM-only, 32 KiB, no RAM. */

    EMIT(0xF3);                   /* DI */
    EMIT(0x31, 0xFE, 0xFF);       /* LD SP,FFFE */
    EMIT(0xAF);                   /* XOR A */
    EMIT(0xE0, 0x40);             /* LCDC=0 for VRAM setup */
    EMIT(0x21, 0x10, 0x80);       /* HL=8010: tile 1 */
    EMIT(0x11, 0x00, 0x03);       /* DE=0300: tile data */
    EMIT(0x06, 0x10);             /* B=16 bytes */
    mark(&a, COPY_TILE);
    EMIT(0x1A, 0x22, 0x13, 0x05); /* Copy a byte, advance, DEC B */
    jr(&a, 0x20, COPY_TILE);      /* JR NZ */
    EMIT(0x3E, 0xE4, 0xE0, 0x47); /* BGP=identity palette */
    EMIT(0x21, 0x2A, 0x99);       /* HL=tilemap position (10,9) */
    EMIT(0x3E, 0x01, 0x77);       /* Draw tile 1 */
    EMIT(0x11, 0xE0, 0xFF);       /* DE=-32, one row up */
    EMIT(0x01, 0x20, 0x00);       /* BC=+32, one row down */
    EMIT(0x3E, 0x91, 0xE0, 0x40); /* Enable LCD and background */

    mark(&a, MAIN_LOOP);
    mark(&a, WAIT_VBLANK);
    EMIT(0xF0, 0x44, 0xFE, 0x90); /* Read LY; compare with 144 */
    jr(&a, 0x20, WAIT_VBLANK);
    EMIT(0xAF, 0x77);             /* Erase old tile */
    EMIT(0x3E, 0x20, 0xE0, 0x00); /* Select direction buttons */
    EMIT(0xF0, 0x00);             /* Read joypad into A */
    EMIT(0xCB, 0x47);             /* BIT 0,A: Right */
    jr(&a, 0x20, NO_RIGHT);
    EMIT(0x23);                   /* INC HL */
    mark(&a, NO_RIGHT);
    EMIT(0xCB, 0x4F);             /* BIT 1,A: Left */
    jr(&a, 0x20, NO_LEFT);
    EMIT(0x2B);                   /* DEC HL */
    mark(&a, NO_LEFT);
    EMIT(0xCB, 0x57);             /* BIT 2,A: Up */
    jr(&a, 0x20, NO_UP);
    EMIT(0x19);                   /* ADD HL,DE */
    mark(&a, NO_UP);
    EMIT(0xCB, 0x5F);             /* BIT 3,A: Down */
    jr(&a, 0x20, NO_DOWN);
    EMIT(0x09);                   /* ADD HL,BC */
    mark(&a, NO_DOWN);
    EMIT(0x3E, 0x01, 0x77);       /* Draw tile at new position */
    mark(&a, WAIT_LEAVE);
    EMIT(0xF0, 0x44, 0xFE, 0x90);
    jr(&a, 0x28, WAIT_LEAVE);     /* One move per VBlank */
    jp(&a, MAIN_LOOP);

    memset(&a.rom[0x300], 0xFF, 16); /* Solid 8x8 square. */
    if (!resolve(&a)) {
        fputs("Could not assemble demo ROM\n", stderr);
        return 1;
    }
    uint8_t checksum = 0;
    for (size_t i = 0x134; i <= 0x14C; ++i)
        checksum = (uint8_t)(checksum - a.rom[i] - 1);
    a.rom[0x14D] = checksum;

    FILE *output = fopen(output_name, "wb");
    if (!output) { perror(output_name); return 1; }
    bool written = fwrite(a.rom, 1, sizeof a.rom, output) == sizeof a.rom;
    if (fclose(output) != 0) written = false;
    if (!written) { fprintf(stderr, "Could not write %s\n", output_name); return 1; }
    printf("Wrote %s\n", output_name);
    return 0;
}
