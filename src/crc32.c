#include "crc32.h"

/*
 * Bitwise CRC-32: no table, so it costs no flash on an MCU. The flash page
 * is small enough that speed doesn't matter. (An MCU's CRC unit can be set
 * to the same CRC: polynomial 0x04C11DB7, input and output reflected.)
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
