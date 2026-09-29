#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

#define BLK_SIZE_BYTES           (BLOCK_SIZE_CELLS * sizeof(uint32_t))

/**
 * Initializes the block mass storage simulation.
 * Checks for file existence; if missing, formats a blank file filled with ASCII spaces
 * and writes the default Block 0 header template. Pads the file to a whole number
 * of blocks, then reads and caches the write-protect slider from the Block 0 header.
 * filename: the block file, or NULL or "" for BLOCKFILENAME.
 * capacity: if not NULL, receives the number of blocks in the file.
 * Returns 0 if okay, or a negative error code (e.g., ERR_BLK_PARSE_FAIL for a bad header).
 */
int blk_init(char *filename, uint32_t *capacity);

/**
 * Reads a single 4KB block payload into the destination buffer.
 * Returns 0 if okay, ERR_BLK_BOUNDS if blk is past the end of the file,
 * or another negative error code.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a single 4KB block payload from the source buffer.
 * Enforces the write-protection slider parsed from Block 0: blocks at or
 * above it cannot be written.
 * Returns 0 if okay, ERR_BLK_BOUNDS if blk is past the end of the file,
 * ERR_BLK_WRITE_PROTECTED, or another negative error code.
 */
int blk_write(uint32_t blk, uint32_t *src);

#endif /* BLOCKS_H */
