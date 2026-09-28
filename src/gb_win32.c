#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GameBoy *active_gb;
static bool running = true;

static char *save_filename(const char *rom_path)
{
    size_t size = strlen(rom_path) + 5;
    char *name = malloc(size);
    if (name) snprintf(name, size, "%s.sav", rom_path);
    return name;
}

static void load_save_ram(GameBoy *gb, const char *name)
{
    if (!name || !gb->cart.battery || !gb->cart.ram_size) return;
    FILE *file = fopen(name, "rb");
    if (!file) return;
    uint8_t *data = malloc(gb->cart.ram_size);
    if (!data) { fclose(file); return; }
    size_t count = fread(data, 1, gb->cart.ram_size, file);
    int extra = fgetc(file);
    fclose(file);
    if (count == gb->cart.ram_size && extra == EOF)
        memcpy(gb->cart.ram, data, count);
    else
        fprintf(stderr, "Ignoring save with unexpected size: %s\n", name);
    free(data);
}

static bool save_ram(GameBoy *gb, const char *name)
{
    if (!name || !gb->cart.battery || !gb->cart.ram_dirty) return true;
    size_t size = strlen(name) + 5;
    char *temporary = malloc(size);
    if (!temporary) return false;
    snprintf(temporary, size, "%s.tmp", name);
    FILE *file = fopen(temporary, "wb");
    bool written = file &&
        fwrite(gb->cart.ram, 1, gb->cart.ram_size, file) == gb->cart.ram_size;
    if (file && fclose(file) != 0) written = false;
    if (written)
        written = MoveFileExA(temporary, name,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!written) fprintf(stderr, "Could not write save: %s\n", name);
    else gb->cart.ram_dirty = false;
    free(temporary);
    return written;
}

static int button_for_key(WPARAM key)
{
    switch (key) {
    case VK_RIGHT: return 0;
    case VK_LEFT: return 1;
    case VK_UP: return 2;
    case VK_DOWN: return 3;
    case 'Z': return 4;       /* A */
    case 'X': return 5;       /* B */
    case VK_SHIFT: return 6;  /* Select */
    case VK_RETURN: return 7; /* Start */
    default: return -1;
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
        if (wparam == VK_ESCAPE && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
            DestroyWindow(window);
            return 0;
        }
        int button = button_for_key(wparam);
        if (button >= 0)
            gb_set_button(active_gb, (unsigned)button,
                          message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
        return 0;
    }
    case WM_KILLFOCUS:
        for (unsigned button = 0; button < 8; ++button)
            gb_set_button(active_gb, button, false);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        BITMAPINFO bitmap = {0};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = GB_SCREEN_WIDTH;
        bitmap.bmiHeader.biHeight = -GB_SCREEN_HEIGHT; /* Top-down pixels. */
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, client.right, client.bottom,
                      0, 0, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT,
                      active_gb->pixels, &bitmap, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(window, message, wparam, lparam);
    }
}

static HWND create_window(HINSTANCE instance)
{
    WNDCLASSA window_class = {0};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "CGameBoyWindow";
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassA(&window_class)) return NULL;
    RECT bounds = {0, 0, GB_SCREEN_WIDTH * 4, GB_SCREEN_HEIGHT * 4};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExA(0, window_class.lpszClassName,
        "Game Boy emulator - Z=A, X=B, Enter=Start, Shift=Select",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        NULL, NULL, instance, NULL);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
    return window;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s cartridge.gb\n", argv[0]);
        return 2;
    }
    GameBoy gb = {0};
    if (!gb_load_rom(&gb, argv[1])) return 1;
    char *save_name = save_filename(argv[1]);
    if (gb.cart.battery && !save_name) {
        fputs("Out of memory creating save filename\n", stderr);
        gb_free(&gb);
        return 1;
    }
    load_save_ram(&gb, save_name);
    active_gb = &gb;
    HWND window = create_window(GetModuleHandleA(NULL));
    if (!window) {
        fprintf(stderr, "Could not create a Windows window (error %lu)\n",
                (unsigned long)GetLastError());
        gb_free(&gb);
        free(save_name);
        return 1;
    }
    LARGE_INTEGER frequency, start, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    uint64_t frame = 0;
    uint64_t next_frame_cycles = 70224;
    int status = 0;
    while (running) {
        MSG event;
        while (PeekMessageA(&event, NULL, 0, 0, PM_REMOVE)) {
            if (event.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&event);
            DispatchMessageA(&event);
        }
        if (!running) break;
        while (gb.cpu.cycles < next_frame_cycles) {
            uint16_t pc = gb.cpu.pc;
            uint8_t opcode;
            if (gb_step(&gb, &opcode) == GB_STEP_ILLEGAL) {
                char error[128];
                snprintf(error, sizeof error,
                         "Illegal opcode %02X at PC=%04X", opcode, pc);
                MessageBoxA(window, error, "Emulator stopped", MB_OK | MB_ICONERROR);
                status = 1;
                running = false;
                break;
            }
        }
        if (!running) break;
        next_frame_cycles += 70224;
        if (gb.frame_ready) {
            gb.frame_ready = false;
            InvalidateRect(window, NULL, FALSE);
            UpdateWindow(window);
        }
        ++frame;
        if (frame % 600 == 0) save_ram(&gb, save_name);
        LONGLONG target = start.QuadPart +
            (LONGLONG)((frame * (uint64_t)frequency.QuadPart * 70224u) / 4194304u);
        QueryPerformanceCounter(&now);
        while (running && now.QuadPart < target) {
            LONGLONG milliseconds =
                (target - now.QuadPart) * 1000 / frequency.QuadPart;
            if (milliseconds > 1) Sleep((DWORD)(milliseconds - 1));
            else Sleep(0);
            QueryPerformanceCounter(&now);
        }
    }
    if (!save_ram(&gb, save_name)) status = 1;
    free(save_name);
    gb_free(&gb);
    return status;
}