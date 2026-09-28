#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t wram[0x2000]; /* 8 KiB: 0xC000 through 0xDFFF */
} Bus;

static bool wram_index(uint16_t address, size_t *index)
{
    if (address >= 0xC000 && address <= 0xDFFF) {
        *index = (size_t)(address - 0xC000);
        return true;
    }
    if (address >= 0xE000 && address <= 0xFDFF) {
        *index = (size_t)(address - 0xE000);
        return true;
    }
    return false;
}

static bool bus_read(const Bus *bus, uint16_t address, uint8_t *value)
{
    size_t index;
    if (!wram_index(address, &index)) {
        return false; /* This lesson has not mapped that address yet. */
    }
    *value = bus->wram[index];
    return true;
}

static bool bus_write(Bus *bus, uint16_t address, uint8_t value)
{
    size_t index;
    if (!wram_index(address, &index)) {
        return false;
    }
    bus->wram[index] = value;
    return true;
}

int main(void)
{
    Bus bus = {0};
    uint8_t value = 0;

    if (!bus_write(&bus, 0xC123, 0x42) ||
        !bus_read(&bus, 0xE123, &value) ||
        value != 0x42) {
        fputs("Memory mapping failed\n", stderr);
        return 1;
    }

    printf("0xE123 reads 0x%02X after writing 0xC123\n", value);
    return 0;
}
