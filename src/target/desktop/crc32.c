#include "crc32.h"

/*
 * Desktop CRC-32, bitwise. Speed doesn't matter for one flash page, and it
 * doubles as a reference for MCU targets that use CRC hardware.
 */
uint32_t lfCrc32(const int32_t* cells, uint32_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    while (n--) {
        uint32_t x = (uint32_t)*cells++;
        for (int b = 0; b < 4; b++) {
            crc ^= x & 0xFFu;
            x >>= 8;
            for (int k = 0; k < 8; k++) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
    }
    return ~crc;
}
