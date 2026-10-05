#ifndef RAWDISK_H
#define RAWDISK_H

#include <stdint.h>

/*
 * Block storage on a raw disk partition, as on the CH32H417's microSD card
 * (doc/sdcard.md): the first MBR entry of type 0xDA holds 4 KB blocks. It
 * must start on a 64 KB boundary, and its block 0 must start with
 * BLK_SIGNATURE; otherwise only block 0 is readable.
 *
 * On Windows, `lf -k F:` uses the removable disk that holds drive F:. Raw
 * disk access needs an administrator. Elsewhere, the disk is an image
 * file (or a whole-disk device such as /dev/sdb), which is how the unit
 * test exercises this code; lf itself only uses it for a drive letter.
 */

/**
 * Tells whether a block file name selects a raw disk: a drive letter such
 * as "F:" or "F:\", on Windows only.
 * Returns 1 if it does, else 0.
 */
int rawdisk_is_drive(const char *name);

/**
 * Opens the block partition on the disk named by `name`: on Windows the
 * removable disk that holds drive `name` ("F:"), elsewhere a disk image
 * file. Prints what it found, or why it failed, on stderr.
 * blocks: receives the number of readable blocks (1 without the signature
 * or the alignment, so block 0 can be looked at).
 * writable: receives 1 if the blocks can be written, else 0.
 * Returns 0 if okay, or ERR_BLK_OPEN_FAIL if the disk can't be opened or
 * has no block partition (type 0xDA).
 */
int rawdisk_open(const char *name, uint32_t *blocks, int *writable);

/**
 * Reads block `blk` (4 KB) of the partition into `dest`. The caller checks
 * the bounds.
 * Returns 0 if okay, ERR_BLK_SEEK_FAIL or ERR_BLK_READ_FAIL.
 */
int rawdisk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes block `blk` (4 KB) of the partition from `src`. The caller checks
 * the bounds and that the partition is writable.
 * Returns 0 if okay, ERR_BLK_SEEK_FAIL or ERR_BLK_WRITE_FAIL.
 */
int rawdisk_write(uint32_t blk, const uint32_t *src);

/**
 * Closes the disk, if one is open.
 */
void rawdisk_close(void);

#endif /* RAWDISK_H */
