/* lcd.c: no LCD driver on the V5F yet (see lcd.h). */
#include "lcd.h"
#include "../../../../errcodes.h"

int lcd_init(int width, int height, int controller, const uint16_t* palette) {
    (void)width;  (void)height;  (void)controller;  (void)palette;
    return ERR_UNSUPPORTED_OPERATION;
}

int lcd_tx(uint16_t data) {
    (void)data;
    return ERR_UNSUPPORTED_OPERATION;
}

int lcd_decode(uint16_t code) {
    (void)code;
    return ERR_UNSUPPORTED_OPERATION;
}
