/* lcd.c: the nanoCH32H417's LCD connector (a 1.54" 240 x 240 ST7789 IPS
   module) on SPI2, behind the same bus interface as the desktop simulator
   (src/target/desktop/lcd.c), so the same Forth drives both.

   Pins (doc/targets.md), as in the board's SPI_LCD example:
     SCLK PB13 and MOSI PB15 (SPI2, AF5), CS PB12, D/C PD9, RES PD8.
   SPI2 runs in mode 3 (CPOL high, CPHA second edge), 8 bits, MSB first,
   at its fastest prescaler. Nothing is read back, so the receive side's
   overrun flag is left alone. Each byte is written as soon as the transmit
   buffer is free; the bus is waited on only before D/C, CS or RES changes,
   so a run of pixels streams back to back. */
#include "ch32h417.h"
#include "lcd.h"
#include "../../../../errcodes.h"

#define CS_PORT     GPIOB
#define CS_PIN      GPIO_Pin_12
#define DC_PORT     GPIOD
#define DC_PIN      GPIO_Pin_9
#define RES_PORT    GPIOD
#define RES_PIN     GPIO_Pin_8

static int lcd_ready;                   /* the pins and SPI2 are set up */
static int lcd_open;                    /* lcd_init succeeded */
static const uint16_t* lcd_palette;     /* lcd_decode's 16 colors, or NULL */
static int dc_now = -1;                 /* the D/CX level: 1 data, 0 command */
static int cs_now = -1;                 /* the CSX level: 1 deselected */
static int res_now = -1;                /* the RESX level: 0 in reset */

static void pin(GPIO_TypeDef* port, uint16_t bit, int high) {
    if (high) port->BSHR = bit;
    else port->BCR = bit;
}

/* Waits until the last byte has left the shift register. */
static void spi_idle(void) {
    while (!(SPI2->STATR & SPI_STATR_TXE)) { }
    while (SPI2->STATR & SPI_STATR_BSY) { }
}

static inline void spi_byte(uint8_t b) {
    while (!(SPI2->STATR & SPI_STATR_TXE)) { }
    SPI2->DATAR = b;
}

/* Sets a control line, after the bytes already sent have gone out. */
static void set_line(int* now, int level, GPIO_TypeDef* port, uint16_t bit) {
    if (*now == level) return;
    spi_idle();
    pin(port, bit, level);
    *now = level;
}

static void hw_init(void) {
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef spi = {0};
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB | RCC_HB2Periph_GPIOD, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SPI2, ENABLE);

    pin(CS_PORT, CS_PIN, 1);            /* deselected, out of reset, data */
    pin(RES_PORT, RES_PIN, 1);
    pin(DC_PORT, DC_PIN, 1);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Pin = RES_PIN | DC_PIN;
    GPIO_Init(GPIOD, &gpio);
    gpio.GPIO_Pin = CS_PIN;
    GPIO_Init(GPIOB, &gpio);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF5);    /* SCLK */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF5);    /* MOSI */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
    GPIO_Init(GPIOB, &gpio);

    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode0;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &spi);
    SPI_NSSInternalSoftwareConfig(SPI2, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI2, ENABLE);
    dc_now = 1;  cs_now = 1;  res_now = 1;
    lcd_ready = 1;
}

int lcd_init(int width, int height, int controller, const uint16_t* palette) {
    if ((controller != LCD_ST7789 && controller != LCD_ST7789_IPS)
        || width < 1 || height < 1 || width > 240 || height > 320) {
        return ERR_UNSUPPORTED_OPERATION;
    }
    if (!lcd_ready) hw_init();
    lcd_palette = palette;
    lcd_open = 1;
    return 0;
}

int lcd_tx(uint16_t data) {
    if (!lcd_open) return ERR_UNSUPPORTED_OPERATION;
    set_line(&res_now, !(data & LCD_RESET), RES_PORT, RES_PIN);
    if (data & LCD_RESET) return 0;                     /* held in reset */
    set_line(&cs_now, (data & LCD_CSX) != 0, CS_PORT, CS_PIN);
    if (data & LCD_CSX) return 0;                       /* not selected */
    set_line(&dc_now, !(data & LCD_CMD), DC_PORT, DC_PIN);
    spi_byte((uint8_t)data);
    return 0;
}

/* Sends count pixels of palette entry v, high byte first. */
static void decode_run(unsigned v, unsigned count) {
    uint16_t c = lcd_palette[v & 15];
    uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)c;
    while (count--) {
        spi_byte(hi);
        spi_byte(lo);
    }
}

int lcd_decode(uint16_t code) {
    if (!lcd_open || !lcd_palette) return ERR_UNSUPPORTED_OPERATION;
    set_line(&res_now, 1, RES_PORT, RES_PIN);
    set_line(&cs_now, 0, CS_PORT, CS_PIN);
    set_line(&dc_now, 1, DC_PORT, DC_PIN);
    switch (code >> 14) {               /* the formats in doc/fonts.md */
    case 0:                             /* gray black[3] gray white[3] */
        decode_run(code >> 10, 1);  decode_run(0, (code >> 7) & 7);
        decode_run(code >> 3, 1);   decode_run(15, code & 7);
        break;
    case 1:                             /* gray next length[9] */
        decode_run(code >> 10, 1);
        decode_run((code >> 9) & 1 ? 15 : 0, code & 511);
        break;
    case 2:                             /* gray gray next length[5] */
        decode_run(code >> 10, 1);  decode_run(code >> 6, 1);
        decode_run((code >> 5) & 1 ? 15 : 0, code & 31);
        break;
    default:                            /* gray gray gray black[2] */
        decode_run(code >> 10, 1);  decode_run(code >> 6, 1);
        decode_run(code >> 2, 1);   decode_run(0, code & 3);
        break;
    }
    return 0;
}
