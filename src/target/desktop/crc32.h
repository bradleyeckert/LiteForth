#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Each target provides lfCrc32. This one is the desktop's, in software; an
 * MCU target can use its CRC unit instead, set to the same CRC (polynomial
 * 0x04C11DB7, input and output reflected, initial value and final XOR
 * 0xFFFFFFFF), feeding cells LSB first (a little-endian MCU's word order).
 *
 * Computes the standard CRC-32 (IEEE 802.3, as used by zlib and PNG:
 * reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF)
 * of an array of cells.
 *
 * Each cell is fed least significant byte first, so the result is the same
 * on any host and equals the CRC-32 of the cells stored little-endian (the
 * byte order of lfflash.bin). Used by `close-flash` to seal the `,wids`
 * record and by the loader to check it.
 *
 * @param cells First cell.
 * @param n     Number of cells.
 * @return The CRC.
 */
uint32_t lfCrc32(const int32_t* cells, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
