#ifndef IO_ALLOW_H
#define IO_ALLOW_H

/*
 * The peripherals Forth may reach in a production build (DEVELOPER_MODE 0
 * in Common/security.h): main.c makes VM page VM_IO_PAGE the guarded page
 * (vm_io_page in vm.h) with this list. Edit it per product. In a
 * development build Forth reaches every register from IO_BASE, as before.
 *
 * Each entry is a range of byte addresses [from, to) and what's allowed:
 * IO_RW, or IO_RO (reads only; a write gives -20). Anything not listed
 * reads as an invalid address (-9).
 *
 * Left out, because Forth could use them to get past LiteForth's checks
 * (doc/security-plan.md 5.6, doc/firmware-updates.md section 8):
 *   - FLASH (the flash controller): unlocking, erasing, the option bytes;
 *   - DMA1, DMA2, DMAMUX, and the peripherals that are bus masters with
 *     their own DMA (LTDC, GPHA, HSADC, SDMMC, USBPD, DVP, SERDES, ETH,
 *     the USB controllers): a transfer can read or write any memory;
 *   - SDIO: raw card sectors, past card.c's fences;
 *   - ECDC (it holds keys while it works) and RNG;
 *   - PWR, and writes to RCC: clocks, resets and power of both cores;
 *   - USBFS, USBHS, USBSS, UHSIF: the V3F's USB terminal;
 *   - QSPI1, QSPI2, FMC: external memory, which may hold firmware;
 *   - PIOC: a core of its own.
 * Read only: RTC, WWDG and IWDG (`rtc!`, `alarm!` and `standby` do the
 * writing), USART1 (the debug UART both cores log on), RCC (clocks and
 * reset flags).
 *
 * GPIO stays writable, so Forth can reconfigure any pin, including the
 * card's and the debug port's. That can stop the board working, but
 * gets no further: what's on the card is sealed and checked.
 */

#define IO_RO   VM_IO_READ
#define IO_RW   (VM_IO_READ | VM_IO_WRITE)

/* Byte addresses -> the cells of the VM page */
#define IO_RANGE(from, to, flags) \
    { ((from) - IO_BASE) / 4, ((to) - IO_BASE) / 4, (flags) }

static const vm_io_range io_allow[] = {
    IO_RANGE(TIM2_BASE,   LPTIM1_BASE + 0x400, IO_RW),  /* TIM2-7, USART6-8, LPTIM1 */
    IO_RANGE(RTC_BASE,    IWDG_BASE + 0x400,   IO_RO),  /* RTC, WWDG, IWDG */
    IO_RANGE(LPTIM2_BASE, I2C3_BASE + 0x400,   IO_RW),  /* LPTIM2, SPI2-4, USART2-5, I2C1-3 */
    IO_RANGE(CAN1_BASE,   CAN2_BASE + 0x400,   IO_RW),  /* CAN1, CAN2 */
    IO_RANGE(DAC_BASE,    CAN3_BASE + 0x400,   IO_RW),  /* DAC, CAN3 */
    IO_RANGE(SWPMI_BASE,  SWPMI_BASE + 0x400,  IO_RW),
    IO_RANGE(AFIO_BASE,   GPIOF_BASE + 0x400,  IO_RW),  /* AFIO, EXTI, GPIOA-F */
    IO_RANGE(ADC1_BASE,   TIM8_BASE + 0x400,   IO_RW),  /* ADC1, ADC2, TIM1, SPI1, TIM8 */
    IO_RANGE(USART1_BASE, USART1_BASE + 0x400, IO_RO),  /* the debug UART */
    IO_RANGE(TIM12_BASE,  I3C_BASE + 0x400,    IO_RW),  /* TIM12, I2C4, I3C */
    IO_RANGE(TIM9_BASE,   SAI_BASE + 0x400,    IO_RW),  /* TIM9-11, SAI */
    IO_RANGE(DFSDM_BASE,  DFSDM_BASE + 0x400,  IO_RW),
    IO_RANGE(OPA_BASE,    OPA_BASE + 0x400,    IO_RW),
    IO_RANGE(RCC_BASE,    RCC_BASE + 0x400,    IO_RO),
    IO_RANGE(CRC_BASE,    CRC_BASE + 0x400,    IO_RW),
    { 0, 0, 0 }
};

#endif /* IO_ALLOW_H */
