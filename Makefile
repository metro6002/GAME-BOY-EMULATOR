CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic -I.
CORE = src/gb_cpu.c src/gb_bus.c src/gb_ppu.c

all: main.exe gbplay.exe

main.exe: src/main.c $(CORE) gb.h
	$(CC) $(CFLAGS) src/main.c $(CORE) -o $@

gbplay.exe: src/gb_win32.c $(CORE) gb.h
	$(CC) $(CFLAGS) src/gb_win32.c $(CORE) -o $@ -lgdi32 -luser32

selftest.exe: tests/selftest.c $(CORE) gb.h
	$(CC) $(CFLAGS) tests/selftest.c $(CORE) -o $@

test: selftest.exe
	./selftest.exe

play: gbplay.exe
	./gbplay.exe "Wario Land_ Super Mario Land 3/Wario Land - Super Mario Land 3 (World).gb"
