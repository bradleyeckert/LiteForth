#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Each target provides lfCrc32. This one, for the STM32H743, uses the CRC
 * unit; src/target/desktop/crc32.c is the software reference.
 *
 * Computes the standard CRC-32 (IEEE 802.3, as used by zlib and PNG:
 * reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF)
 * of an array of cells, each fed least significant byte first. The result
 * is the same as the desktop's, so a flash image built on either can be
 * checked on the other. Used by `close-flash` to seal the `,wids` record and
 * by the loader to check it.
 *
 * Not reentrant: it reprograms the CRC unit, so don't call it from an
 * interrupt or while other code is using the unit.
 *
 * @param cells First cell (any 4-byte aligned address, including flash).
 * @param n     Number of cells.
 * @return The CRC.
 */
uint32_t lfCrc32(const int32_t* cells, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
