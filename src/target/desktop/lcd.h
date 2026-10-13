#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A simulated graphic LCD module for the desktop lf. The module's
 * controller is emulated at its bus interface (lcd_tx), so Forth code
 * written against it drives a real module the same way. The panel is
 * shown in a window: Win32 on Windows, X11 elsewhere (built with LCD_X11,
 * which the makefile defines when pkg-config finds x11). Without either,
 * lcd_init reports ERR_UNSUPPORTED_OPERATION. LCD_HEADLESS builds the
 * emulation without a window, for tests.
 */

#define LCD_ZOOM            2   // window pixels per LCD pixel, each way

#define LCD_ST7789          0   // controller: Sitronix ST7789 (240 x 320)

// lcd_tx bus bits
#define LCD_RESET      0x8000   // reset: 1 = held in reset (the RESX pin low)
#define LCD_CSX        0x4000   // chip select: 0 = selected
#define LCD_DCX        0x2000   // data/command: 0 = command, 1 = data
#define LCD_DATA       0x00FF   // the byte on the bus

/**
 * Opens the LCD window and powers up the controller. The controller
 * starts in its reset state (sleeping, display off), as after power-on,
 * with random contents in its frame memory.
 *
 * @param width      Panel width in pixels (the controller's columns).
 * @param height     Panel height in pixels (its rows). The panel shows
 *                   frame memory columns 0..width-1 and rows 0..height-1.
 *                   The window is LCD_ZOOM times as big.
 * @param controller The controller to emulate: LCD_ST7789 (frame memory
 *                   240 x 320, so width <= 240 and height <= 320).
 * @return 0 if the window is open, or ERR_UNSUPPORTED_OPERATION if it
 *         can't be opened (no display, unknown controller, panel too big
 *         for the controller). Calling it again replaces the panel.
 */
int lcd_init(int width, int height, int controller);

/**
 * Drives the controller's bus for one transfer.
 *
 * Bit 15 is reset (active high: the inverse of the RESX pin), bit 14
 * chip select CSX and bit 13 the data/command pin D/CX; bits 7:0 are the
 * byte on the bus (an 8-bit 8080 parallel bus, or one 8-bit SPI transfer;
 * for 3-wire 9-bit SPI, D/CX is the first bit). Bits 12:8 are ignored.
 *   - Reset = 1 resets the controller and holds it there; normal
 *     transfers have bit 15 = 0.
 *   - CSX = 1 ignores the byte.
 *   - D/CX = 0 sends a command, 1 a parameter or pixel data.
 * Reading from the controller isn't simulated.
 *
 * @param data The pins and the byte.
 * @return 0, or ERR_UNSUPPORTED_OPERATION if lcd_init hasn't succeeded.
 */
int lcd_tx(uint16_t data);

/**
 * Reads what the panel shows at a pixel, after display on/off, sleep,
 * inversion, idle mode and scrolling.
 *
 * @param x Column, 0..width-1.
 * @param y Row, 0..height-1.
 * @return The color as 0xRRGGBB (each component expanded from the
 *         controller's 6 bits), or -1 if (x, y) is off the panel or there
 *         is no panel.
 */
int32_t lcd_pixel(int x, int y);

/**
 * Saves what the panel shows as a binary PPM image (P6), one pixel per
 * LCD pixel.
 *
 * @param filename The file to write.
 * @return 0, ERR_UNSUPPORTED_OPERATION if there is no panel, or
 *         ERR_CREATE_FILE_FAILED if the file can't be written.
 */
int lcd_save(const char* filename);

/**
 * Closes the window and frees the panel. lcd_tx then reports
 * ERR_UNSUPPORTED_OPERATION until the next lcd_init.
 */
void lcd_close(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_H */
