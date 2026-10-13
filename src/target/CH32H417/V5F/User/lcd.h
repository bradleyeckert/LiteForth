#ifndef LCD_H
#define LCD_H

#include <stdint.h>

/*
 * The LCD interface (see src/target/desktop/lcd.h, which simulates one).
 * The V5F has no LCD driver yet: lcd.c only reports that, so `lcd-init`
 * and `lcd-tx` fail with ERR_UNSUPPORTED_OPERATION. A driver would put
 * lcd_tx's pins and byte on GPIO or SPI.
 */

#define LCD_ST7789          0   // controller: Sitronix ST7789

// lcd_tx bus bits
#define LCD_RESET      0x8000   // reset: 1 = held in reset (the RESX pin low)
#define LCD_CSX        0x4000   // chip select: 0 = selected
#define LCD_CMD        0x2000   // command: 1 = command, 0 = data (the D/CX pin low)
#define LCD_DATA       0x00FF   // the byte on the bus

/**
 * Starts the LCD.
 * @return ERR_UNSUPPORTED_OPERATION: no driver yet.
 */
int lcd_init(int width, int height, int controller);

/**
 * One transfer on the LCD bus: bit 15 reset (1 = RESX low), 14 CSX,
 * 13 command (1 = D/CX low), 7:0 the byte.
 * @return ERR_UNSUPPORTED_OPERATION: no driver yet.
 */
int lcd_tx(uint16_t data);

#endif /* LCD_H */
