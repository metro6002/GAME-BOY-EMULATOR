CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic
CORE = gb_cpu.c gb_bus.c gb_ppu.c

all: gbemu.exe gbplay.exe demo.gb

gbemu.exe: gbemu.c $(CORE) gb.h
	$(CC) $(CFLAGS) gbemu.c $(CORE) -o $@

gbplay.exe: gb_win32.c $(CORE) gb.h
	$(CC) $(CFLAGS) gb_win32.c $(CORE) -o $@ -lgdi32 -luser32

selftest.exe: selftest.c $(CORE) gb.h
	$(CC) $(CFLAGS) selftest.c $(CORE) -o $@

demo_rom.exe: demo_rom.c
	$(CC) $(CFLAGS) demo_rom.c -o $@

demo.gb: demo_rom.exe
	./demo_rom.exe demo.gb

playtest.exe: playtest.c $(CORE) gb.h
	$(CC) $(CFLAGS) playtest.c $(CORE) -o $@

test: selftest.exe demo.gb playtest.exe
	./selftest.exe
	./playtest.exe
