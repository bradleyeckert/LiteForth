/*
 * The CH32H417's peripheral registers as a VM memory page (see iopage.h).
 *
 * The table lists the 1 KB slots that hold a peripheral, as byte offsets
 * from IO_BASE, merged where they touch. It was made from the *_BASE and
 * *_TypeDef definitions in SRC/Peripheral/inc/ch32h417.h: each peripheral's
 * base, rounded down to 1 KB, to the end of its register struct, rounded up
 * to 1 KB. A whole slot is allowed, since a peripheral decodes its own slot;
 * the gaps between slots have nothing behind them.
 */
#include "iopage.h"

static const struct { uint32_t start, end; } io_ranges[] = {
    { 0x00000, 0x06000 },   /* TIM2-7 USART2-8 LPTIM1-2 RTC WWDG IWDG SPI2-4 I2C1-3 */
    { 0x06400, 0x06C00 },   /* CAN1 CAN2 */
    { 0x07000, 0x07C00 },   /* PWR DAC CAN3 */
    { 0x08400, 0x08800 },   /* SWPMI */
    { 0x10000, 0x12000 },   /* AFIO EXTI GPIOA-F */
    { 0x12400, 0x15C00 },   /* ADC1-2 TIM1 SPI1 TIM8 USART1 TIM12 I2C4 I3C LTDC TIM9-11 SAI */
    { 0x16800, 0x17C00 },   /* GPHA ECDC DFSDM HSADC OPA */
    { 0x18000, 0x18400 },   /* SDIO */
    { 0x20000, 0x20C00 },   /* DMA1 DMA2 DMAMUX */
    { 0x21000, 0x21400 },   /* RCC */
    { 0x22000, 0x22400 },   /* FLASH */
    { 0x23000, 0x23800 },   /* CRC USBFS */
    { 0x23C00, 0x24800 },   /* RNG SDMMC USBPD */
    { 0x24C00, 0x26000 },   /* QSPI1-2 FMC DVP PIOC */
    { 0x27C00, 0x29400 },   /* SERDES ETH */
    { 0x30000, 0x30400 },   /* USBHS */
    { 0x34000, 0x34400 },   /* USBSS */
    { 0x38000, 0x38400 },   /* UHSIF */
};

int vmIoValid(uint32_t cell) {
    uint32_t offset = cell << 2;
    for (unsigned i = 0; i < sizeof(io_ranges) / sizeof(io_ranges[0]); i++) {
        if (offset < io_ranges[i].start) return 0;  /* sorted: in a gap */
        if (offset < io_ranges[i].end) return 1;
    }
    return 0;
}
