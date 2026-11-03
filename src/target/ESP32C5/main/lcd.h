#ifndef LCD_H
#define LCD_H

#include <stdint.h>

/*
 * The LCD interface (see src/target/desktop/lcd.h, which simulates one).
 * On the V5F, lcd.c drives the nanoCH32H417's LCD connector: an ST7789
 * module on SPI2, with the pins in lcd.c.
 */

#define LCD_ST7789          0   // controller: Sitronix ST7789
#define LCD_ST7789_IPS      1   // ST7789 with an IPS panel (needs INVON)

// lcd_tx bus bits
#define LCD_RESET      0x8000   // reset: 1 = held in reset (the RESX pin low)
#define LCD_CSX        0x4000   // chip select: 0 = selected
#define LCD_CMD        0x2000   // command: 1 = command, 0 = data (the D/CX pin low)
#define LCD_DATA       0x00FF   // the byte on the bus

/**
 * Sets up the pins and SPI2 (the first time) and keeps the palette. The
 * controller itself starts as it is; reset it with lcd_tx.
 * @param width, height Panel size: up to 240 x 320.
 * @param controller    LCD_ST7789 or LCD_ST7789_IPS (the same to the bus).
 * @param palette       The 16 RGB565 colors lcd_decode sends, or NULL.
 *                      Kept, not copied.
 * @return 0, or ERR_UNSUPPORTED_OPERATION for an unknown controller or a
 *         size the ST7789 can't have.
 */
int lcd_init(int width, int height, int controller, const uint16_t* palette);

/**
 * One transfer on the LCD bus: bit 15 reset (1 = RESX low), 14 CSX,
 * 13 command (1 = D/CX low), 7:0 the byte, sent on SPI2.
 * @return 0, or ERR_UNSUPPORTED_OPERATION before lcd_init.
 */
int lcd_tx(uint16_t data);

/**
 * Expands a font pixel code (doc/fonts.md) into pixels, each the palette
 * entry given to lcd_init for its value, and sends them as data, high byte
 * first: the controller must be in a RAMWR with 16-bit pixels.
 * @return 0, or ERR_UNSUPPORTED_OPERATION before lcd_init or without a
 *         palette.
 */
int lcd_decode(uint16_t code);

#endif /* LCD_H */
