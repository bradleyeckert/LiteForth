#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

#define BLK_SIZE_BYTES           (BLOCK_SIZE_CELLS * sizeof(uint32_t))

/* The first bytes of block 0 of every LiteForth block device. Without
   them, the device isn't LiteForth's: it reports 1 block, which can be
   read but not written, so its block 0 can be looked at but nothing on
   it is changed. */
#define BLK_SIGNATURE            "LITEFORTH"
#define BLK_SIGNATURE_LEN        9

/**
 * Initializes the block mass storage simulation.
 * If the file doesn't exist, creates it: SIMNUMBLOCKS blocks of ASCII
 * spaces, with BLK_SIGNATURE at the start of block 0. A file that starts
 * with BLK_SIGNATURE is padded with spaces to a whole number of blocks,
 * and all its blocks can be read and written. A file that doesn't is left
 * untouched and offers only block 0, read-only.
 * filename: the block file, or NULL or "" for BLOCKFILENAME. On Windows,
 * a drive letter ("F:") selects the block partition of that removable
 * disk instead (rawdisk.h); it fails if the disk can't be opened or has no
 * block partition, and is never created or padded.
 * capacity: if not NULL, receives the number of blocks (1 or 0 for a file
 * without the signature).
 * Returns 0 if okay, or a negative error code if the file can't be
 * created or read.
 */
int blk_init(char *filename, uint32_t *capacity);

/**
 * Reads a single 4KB block payload into the destination buffer, and waits
 * until it's done (blk_read_imm, then blk_status). A transfer still in
 * progress is waited for first.
 * Returns 0 if okay, ERR_BLK_BOUNDS if blk is past the capacity,
 * or another negative error code.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a single 4KB block payload from the source buffer, and waits
 * until it's done (blk_write_imm, then blk_status). A transfer still in
 * progress is waited for first.
 * Returns 0 if okay, ERR_BLK_WRITE_PROTECTED if the file doesn't start
 * with BLK_SIGNATURE, ERR_BLK_BOUNDS if blk is past the capacity, or
 * another negative error code. Overwriting the signature in block 0 takes
 * effect at the next blk_init.
 */
int blk_write(uint32_t blk, uint32_t *src);

/**
 * Starts reading block blk into dest (BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. dest must
 * stay valid and untouched until then.
 * On the desktop the transfer is done before this returns.
 * Returns 0 if the read started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_BLK_BOUNDS if blk is past the capacity, or another
 * negative error code that shows up at once.
 */
int blk_read_imm(uint32_t blk, uint32_t *dest);

/**
 * Starts writing block blk from src (BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. src must
 * stay valid and unchanged until then.
 * On the desktop the transfer is done before this returns.
 * Returns 0 if the write started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_BLK_WRITE_PROTECTED, ERR_BLK_BOUNDS, or another negative
 * error code that shows up at once.
 */
int blk_write_imm(uint32_t blk, uint32_t *src);

/**
 * The state of the last transfer started by blk_read_imm or blk_write_imm
 * (blk_read and blk_write use them too).
 * Returns 0 if it finished without error (or none was started), a negative
 * error code if it failed, 1 while a read is in progress, or 2 while a
 * write is in progress. The result stays until the next transfer starts.
 */
int blk_status(void);

#endif /* BLOCKS_H */
