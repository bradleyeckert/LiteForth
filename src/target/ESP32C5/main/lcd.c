/*
 * LCD stub for the ESP32-C5 -- milestone 1: no panel driver yet.
 *
 * iomap.c (the `lcd-tx` bus) and api0.c (`LCD-INIT`, `lcd-decode`) reference
 * these, so the core links; the real ST7789 driver over SPI2 is milestone 2
 * and needs the NM-CYD-C5 GPIO map. lcd_init reports "no LCD" (-21), as the
 * desktop build does when it has no window. See doc/ESP32C5/esp32c5-notes.md.
 */
#include "lcd.h"

int lcd_init(int width, int height, int controller, const uint16_t* palette) {
    (void)width; (void)height; (void)controller; (void)palette;
    return -21;                 /* no LCD available (milestone 2) */
}

int lcd_tx(uint16_t data) {
    (void)data;
    return 0;                   /* no bus yet; accept and drop */
}

int lcd_decode(uint16_t code) {
    (void)code;
    return 0;
}
