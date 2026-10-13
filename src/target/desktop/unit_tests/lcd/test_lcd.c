/* Unit tests for src/target/desktop/lcd.c (the ST7789 model, built with
   LCD_HEADLESS so no window opens). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "../../lcd.h"
#include "errcodes.h"

#define RED   0xFF0000
#define GREEN 0x00FF00
#define BLUE  0x0000FF
#define BLACK 0x000000
#define WHITE 0xFFFFFF

static void cmd(uint8_t c)  { TEST_ASSERT_EQUAL_INT(0, lcd_tx(LCD_CMD | c)); }
static void dat(uint8_t d)  { TEST_ASSERT_EQUAL_INT(0, lcd_tx(d)); }
static void dat16(int v)    { dat((uint8_t)(v >> 8));  dat((uint8_t)v); }

static void window(int x0, int y0, int x1, int y1) {
    cmd(0x2A);  dat16(x0);  dat16(x1);                  // CASET
    cmd(0x2B);  dat16(y0);  dat16(y1);                  // RASET
}

static void fill565(int x0, int y0, int x1, int y1, uint16_t color) {
    window(x0, y0, x1, y1);
    cmd(0x2C);                                          // RAMWR
    for (int i = 0; i < (x1 - x0 + 1) * (y1 - y0 + 1); i++) dat16(color);
}

/* A typical init: wake, 16-bit color, normal orientation, black, on. */
static void start(int width, int height) {
    TEST_ASSERT_EQUAL_INT(0, lcd_init(width, height, LCD_ST7789));
    cmd(0x11);                                          // SLPOUT
    cmd(0x3A);  dat(0x55);                              // COLMOD 16 bits
    cmd(0x36);  dat(0x00);                              // MADCTL
    fill565(0, 0, 239, 319, 0x0000);
    cmd(0x29);                                          // DISPON
}

void setUp(void) { }
void tearDown(void) { lcd_close(); }

void test_init_checks_arguments(void) {
    TEST_ASSERT_EQUAL_INT(ERR_UNSUPPORTED_OPERATION, lcd_init(240, 320, 1));
    TEST_ASSERT_EQUAL_INT(ERR_UNSUPPORTED_OPERATION, lcd_init(241, 320, LCD_ST7789));
    TEST_ASSERT_EQUAL_INT(ERR_UNSUPPORTED_OPERATION, lcd_init(240, 321, LCD_ST7789));
    TEST_ASSERT_EQUAL_INT(ERR_UNSUPPORTED_OPERATION, lcd_tx(0));
    TEST_ASSERT_EQUAL_INT(0, lcd_init(135, 240, LCD_ST7789));
    TEST_ASSERT_EQUAL_INT(-1, lcd_pixel(135, 0));
    TEST_ASSERT_EQUAL_INT(-1, lcd_pixel(0, 240));
}

void test_starts_dark_until_display_on(void) {
    TEST_ASSERT_EQUAL_INT(0, lcd_init(240, 320, LCD_ST7789));
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(100, 100));   // sleeping, display off
    cmd(0x11);
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(100, 100));   // display still off
    cmd(0x29);
    window(100, 100, 100, 100);
    cmd(0x2C);  dat(0xFC);  dat(0);  dat(0);            // after reset, pixels are 18 bits
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(100, 100));
}

void test_fill_rectangle_565(void) {
    start(240, 320);
    fill565(10, 20, 19, 29, 0xF800);
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(10, 20));
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(19, 29));
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(20, 20));
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(10, 30));
    fill565(0, 0, 0, 0, 0x07E0);
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 0));
    fill565(0, 0, 0, 0, 0x001F);
    TEST_ASSERT_EQUAL_HEX32(BLUE, lcd_pixel(0, 0));
    fill565(0, 0, 0, 0, 0x8410);                        // gray: R 16/31 G 32/63 B 16/31
    TEST_ASSERT_EQUAL_HEX32(0x868286, lcd_pixel(0, 0));
}

void test_18_and_12_bit_pixels(void) {
    start(240, 320);
    cmd(0x3A);  dat(0x66);                              // 18 bits: 3 bytes
    window(5, 5, 5, 5);
    cmd(0x2C);  dat(0xFC);  dat(0x80);  dat(0x04);      // R 63, G 32, B 1
    TEST_ASSERT_EQUAL_HEX32(0xFF8204, lcd_pixel(5, 5));
    cmd(0x3A);  dat(0x53);                              // 12 bits: 2 pixels in 3 bytes
    window(6, 5, 7, 5);
    cmd(0x2C);  dat(0xF0);  dat(0x00);  dat(0xF0);      // red, then green
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(6, 5));
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(7, 5));
}

void test_write_position_wraps_and_ramwrc_continues(void) {
    start(240, 320);
    window(0, 0, 1, 1);                                 // 2 x 2
    cmd(0x2C);
    dat16(0xF800);  dat16(0x07E0);  dat16(0x001F);      // (0,0) (1,0) (0,1)
    cmd(0x3C);  dat16(0xFFFF);                          // RAMWRC: (1,1)
    dat16(0x07E0);                                      // wraps to (0,0)
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 0));
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(1, 0));
    TEST_ASSERT_EQUAL_HEX32(BLUE, lcd_pixel(0, 1));
    TEST_ASSERT_EQUAL_HEX32(WHITE, lcd_pixel(1, 1));
}

void test_madctl_orientations(void) {
    start(240, 320);
    cmd(0x36);  dat(0x40);                              // MX
    fill565(0, 0, 0, 0, 0xF800);
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(239, 0));
    cmd(0x36);  dat(0x80);                              // MY
    fill565(0, 0, 0, 0, 0x07E0);
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 319));
    cmd(0x36);  dat(0x60);                              // MV MX: landscape 320 x 240
    fill565(5, 0, 5, 0, 0x001F);                        // column 5, row 0
    TEST_ASSERT_EQUAL_HEX32(BLUE, lcd_pixel(239, 5));
    fill565(319, 239, 319, 239, 0xFFFF);                // the far corner
    TEST_ASSERT_EQUAL_HEX32(WHITE, lcd_pixel(0, 319));
    cmd(0x36);  dat(0x08);                              // BGR: red data shows blue
    fill565(1, 1, 1, 1, 0xF800);
    TEST_ASSERT_EQUAL_HEX32(BLUE, lcd_pixel(1, 1));
}

void test_240x240_panel_shows_the_top_rows(void) {
    start(240, 240);
    cmd(0x36);  dat(0x80);                              // MY: row 0 is frame row 319,
    fill565(0, 0, 0, 0, 0xF800);                        // off this panel
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(0, 239));
    fill565(0, 80, 0, 80, 0xF800);                      // so MY needs an 80-row offset
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(0, 239));
}

void test_display_modes(void) {
    start(240, 320);
    fill565(3, 3, 3, 3, 0x8410);
    cmd(0x21);                                          // INVON
    TEST_ASSERT_EQUAL_HEX32(WHITE, lcd_pixel(0, 0));
    TEST_ASSERT_EQUAL_HEX32(0x797D79, lcd_pixel(3, 3));
    cmd(0x20);                                          // INVOFF
    cmd(0x39);                                          // IDMON: 8 colors
    TEST_ASSERT_EQUAL_HEX32(WHITE, lcd_pixel(3, 3));
    cmd(0x38);
    TEST_ASSERT_EQUAL_HEX32(0x868286, lcd_pixel(3, 3));
    cmd(0x28);                                          // DISPOFF
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(3, 3));
    cmd(0x29);
    cmd(0x10);                                          // SLPIN
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(3, 3));
    cmd(0x11);
    TEST_ASSERT_EQUAL_HEX32(0x868286, lcd_pixel(3, 3));
}

void test_vertical_scroll(void) {
    start(240, 320);
    fill565(0, 10, 0, 10, 0xF800);
    fill565(0, 0, 0, 0, 0x07E0);
    cmd(0x33);  dat16(0);  dat16(320);  dat16(0);       // VSCRDEF: all of it scrolls
    cmd(0x37);  dat16(10);                              // VSCSAD: row 10 at the top
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(0, 0));
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 310));  // wraps around
    cmd(0x13);                                          // NORON ends scrolling
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 0));
    cmd(0x33);  dat16(20);  dat16(300);  dat16(0);      // 20 fixed rows on top
    cmd(0x37);  dat16(30);
    TEST_ASSERT_EQUAL_HEX32(GREEN, lcd_pixel(0, 0));    // fixed
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(0, 10));
}

void test_pins(void) {
    start(240, 320);
    fill565(0, 0, 0, 0, 0xF800);
    TEST_ASSERT_EQUAL_INT(0, lcd_tx(LCD_CSX | LCD_CMD | 0x28));     // not selected
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(0, 0));
    TEST_ASSERT_EQUAL_INT(0, lcd_tx(LCD_RESET));        // reset
    TEST_ASSERT_EQUAL_INT(0, lcd_tx(LCD_CSX));
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(0, 0));    // asleep and off again
    cmd(0x11);  cmd(0x29);
    TEST_ASSERT_EQUAL_HEX32(RED, lcd_pixel(0, 0));      // frame memory kept
    cmd(0x01);                                          // SWRESET
    TEST_ASSERT_EQUAL_HEX32(BLACK, lcd_pixel(0, 0));
}

void test_save_ppm(void) {
    start(16, 8);
    fill565(0, 0, 0, 0, 0xF800);
    TEST_ASSERT_EQUAL_INT(0, lcd_save("test_lcd.ppm"));
    FILE* f = fopen("test_lcd.ppm", "rb");
    TEST_ASSERT_NOT_NULL(f);
    char head[16] = { 0 };
    TEST_ASSERT_EQUAL_INT(11, (int)fread(head, 1, 11, f));
    TEST_ASSERT_EQUAL_STRING("P6\n16 8\n255", head);
    unsigned char px[4];
    TEST_ASSERT_EQUAL_INT(4, (int)fread(px, 1, 4, f));
    TEST_ASSERT_EQUAL_HEX8(0xFF, px[1]);                // red
    TEST_ASSERT_EQUAL_HEX8(0x00, px[2]);
    fseek(f, 0, SEEK_END);
    TEST_ASSERT_EQUAL_INT(11 + 1 + 16 * 8 * 3, (int)ftell(f));
    fclose(f);
    remove("test_lcd.ppm");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_checks_arguments);
    RUN_TEST(test_starts_dark_until_display_on);
    RUN_TEST(test_fill_rectangle_565);
    RUN_TEST(test_18_and_12_bit_pixels);
    RUN_TEST(test_write_position_wraps_and_ramwrc_continues);
    RUN_TEST(test_madctl_orientations);
    RUN_TEST(test_240x240_panel_shows_the_top_rows);
    RUN_TEST(test_display_modes);
    RUN_TEST(test_vertical_scroll);
    RUN_TEST(test_pins);
    RUN_TEST(test_save_ppm);
    return UNITY_END();
}
