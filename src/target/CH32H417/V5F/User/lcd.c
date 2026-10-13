/* lcd.c: no LCD driver on the V5F yet (see lcd.h). */
#include "lcd.h"
#include "../../../../errcodes.h"

int lcd_init(int width, int height, int controller) {
    (void)width;  (void)height;  (void)controller;
    return ERR_UNSUPPORTED_OPERATION;
}

int lcd_tx(uint16_t data) {
    (void)data;
    return ERR_UNSUPPORTED_OPERATION;
}
