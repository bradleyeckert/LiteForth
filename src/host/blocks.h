#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

/* Fixed block size as per specification: 0x1000 bytes = 4096 bytes = 1024 uint32_t cells */
#define BLK_SIZE_BYTES           4096
#define BLK_SIZE_CELLS           (BLK_SIZE_BYTES / sizeof(uint32_t))

/**
 * Initializes the block mass storage simulation.
 * Checks for file existence; if missing, formats a blank file filled with ASCII spaces
 * and writes the default Block 0 header template. Reads and caches write-protect sliders.
 * Returns 0 if okay, or a negative error code.
 */
int blk_init(char *filename);

/**
 * Reads a single 4KB block payload into the destination buffer.
 * Returns 0 if okay, or a negative error code.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a single 4KB block payload from the source buffer.
 * Enforces hardware write-protection limits parsed from Block 0.
 * Returns 0 if okay, or a negative error code (e.g., ERR_BLK_WRITE_PROTECTED).
 */
int blk_write(uint32_t blk, uint32_t *src);

#endif /* BLOCKS_H */
