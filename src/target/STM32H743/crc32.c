#include "stm32h7xx_hal.h"
#include "crc32.h"

/*
 * CRC-32 on the STM32H743 CRC unit, using its registers directly, so the
 * HAL CRC module doesn't need to be enabled in CubeMX.
 *
 * The unit shifts each 32-bit write in MSB first. The standard CRC-32 takes
 * byte 0 first, bit 0 first, which is the reverse of the whole word, so
 * REV_IN is set to reverse by word (11). REV_OUT reflects the result, and
 * the final XOR is done here: the unit has no output XOR.
 *
 * The CRC unit is on AHB4 on the H743; the clock is left enabled.
 */
uint32_t lfCrc32(const int32_t* cells, uint32_t n) {
    RCC->AHB4ENR |= RCC_AHB4ENR_CRCEN;
    (void)RCC->AHB4ENR;                         // let the clock settle

    CRC->POL  = 0x04C11DB7u;                    // CRC-32 polynomial
    CRC->INIT = 0xFFFFFFFFu;
    CRC->CR   = CRC_CR_REV_IN                   // bit reversal by word
              | CRC_CR_REV_OUT                  // reflected output
              | CRC_CR_RESET;                   // POLYSIZE 00 = 32 bits; load INIT

    while (n--) {
        CRC->DR = (uint32_t)*cells++;
    }
    return ~CRC->DR;
}
