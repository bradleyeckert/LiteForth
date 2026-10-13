/* lcd_demo: opens the LCD window and drives the ST7789 model through
   lcd_tx, the way Forth code will: color bars, a gradient and a scrolling
   band, for a few seconds (default 10, or the number given), then saves
   what the panel shows to lcd_demo.ppm.
   Build with `make demo` in this directory. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "lcd.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static void pause_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void pause_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

static void cmd(uint8_t c) { lcd_tx(c); }
static void dat(uint8_t d) { lcd_tx(LCD_DCX | d); }
static void dat16(int v)   { dat((uint8_t)(v >> 8));  dat((uint8_t)v); }

static void window(int x0, int y0, int x1, int y1) {
    cmd(0x2A);  dat16(x0);  dat16(x1);
    cmd(0x2B);  dat16(y0);  dat16(y1);
    cmd(0x2C);
}

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int main(int argc, char** argv) {
    int seconds = (argc > 1) ? atoi(argv[1]) : 10;
    int ior = lcd_init(240, 320, LCD_ST7789);
    if (ior) {
        printf("lcd_init failed (%d): no window here\n", ior);
        return 1;
    }
    pause_ms(500);                      // the power-on garbage, for a moment
    lcd_tx(LCD_RESET);  lcd_tx(LCD_CSX);                // hardware reset
    cmd(0x11);                          // SLPOUT
    cmd(0x3A);  dat(0x55);              // 16-bit pixels
    cmd(0x36);  dat(0x00);              // MADCTL: portrait
    static const uint16_t bars[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                      0xF81F, 0xF800, 0x001F, 0x0000 };
    window(0, 0, 239, 159);             // color bars
    for (int y = 0; y < 160; y++) {
        for (int x = 0; x < 240; x++) dat16(bars[x / 30]);
    }
    window(0, 160, 239, 319);           // a gradient
    for (int y = 0; y < 160; y++) {
        for (int x = 0; x < 240; x++) dat16(rgb565(x, y * 255 / 159, 255 - x));
    }
    cmd(0x29);                          // DISPON
    cmd(0x33);  dat16(0);  dat16(320);  dat16(0);        // the whole panel scrolls
    for (int t = 0; t < seconds * 30; t++) {
        cmd(0x37);  dat16(t % 320);     // VSCSAD
        pause_ms(33);
    }
    cmd(0x13);                          // NORON: stop scrolling
    lcd_save("lcd_demo.ppm");
    printf("saved lcd_demo.ppm\n");
    lcd_close();
    return 0;
}
