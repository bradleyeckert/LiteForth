#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

#define BLK_SIZE_BYTES           (BLOCK_SIZE_CELLS * sizeof(uint32_t))

/*
 * Block mass storage for the CH32H417 (V5F). Same interface as
 * src/target/desktop/blocks.h. There is no block storage yet: the device
 * has no blocks, so every read or write is out of bounds.
 */

/**
 * Reports an empty block device.
 * filename: ignored.
 * capacity: if not NULL, receives 0.
 * Returns 0.
 */
int blk_init(char *filename, uint32_t *capacity);

/**
 * Reads a block. There are none.
 * Returns ERR_BLK_BOUNDS.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a block. There are none.
 * Returns ERR_BLK_BOUNDS.
 */
int blk_write(uint32_t blk, uint32_t *src);

#endif /* BLOCKS_H */
