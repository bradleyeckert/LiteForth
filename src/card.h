#ifndef CARD_H
#define CARD_H

#include <stdint.h>
#include "exfat.h"

/*
 * Where the block space is on a card (an SD card, or a disk image on the
 * desktop), shared by the targets' blocks.c: CARD_BLOCKS_FILE
 * (lfblocks.fb4) in the root directory of the card's exFAT volume, if it
 * is contiguous. Block n is bytes n * 4096 on of the file, and the
 * capacity is its ValidDataLength / 4096. Block 0 must start with
 * BLK_SIGNATURE for the blocks to be writable; without it only block 0
 * can be read. Either way `space` is the file's size in blocks, which
 * `-1 ymodem` can fill with a whole new block file (blk_raw_write).
 */

#define CARD_BLOCKS_FILE    "lfblocks.fb4"
#define CARD_STAGE_FILE     "lfstage.bin"   /* a firmware update package, by -2 ymodem */
#define CARD_PAGES_FILE     "lfpages.bin"   /* LiteForth's flash pages, by save-pages */
#define CARD_BACKUP_FILE    "lfbackup.bin"  /* stage 1's backup of the V5F image */
#define CARD_UPDATE_FILE    "update.lfu"    /* a package stage 1 installs at boot if newer */

enum { CARD_NONE, CARD_EXFAT_FILE };

typedef struct {
    int kind;                   /* CARD_NONE or CARD_EXFAT_FILE */
    uint32_t base;              /* card sector of block 0 */
    uint32_t blocks;            /* blocks that can be read */
    uint32_t space;             /* blocks the file holds, whatever is in them */
    int writable;               /* block 0 has the signature */
    int mounted;                /* an exFAT volume was found: vol is valid */
    exfat_vol vol;
    exfat_file file;            /* the blocks file */
} card_map;

/**
 * Finds the block space on a card of card_sectors sectors (0 if unknown)
 * through `read`, and describes it in m. msg receives a line saying what
 * was found or why nothing was, without a newline.
 * Returns 0 (m->kind may be CARD_NONE), or the read function's error.
 */
int card_find_blocks(exfat_read_fn read, uint32_t card_sectors, card_map *m,
                     char *msg, int msg_len);

/**
 * Finds another file in the root directory of the volume card_find_blocks
 * found (m->mounted): one made in a PC, contiguous, that the board writes
 * in place, such as CARD_STAGE_FILE. *base receives its first sector and
 * *blocks its size in 4 KB blocks (ValidDataLength / 4096).
 * Returns 0, ERR_FILE_NOT_FOUND (no volume, or no such file),
 * ERR_BLK_PARSE_FAIL (a directory, fragmented, shorter than a block, or
 * past the end of the card), or the read function's error.
 */
int card_find_file(exfat_read_fn read, uint32_t card_sectors, const card_map *m,
                   const char *name, uint32_t *base, uint32_t *blocks);

/**
 * Lists the root directory of the volume card_find_blocks found
 * (m->mounted) through exfat_list: fn gets each name and entry.
 * Returns 0, ERR_FILE_NOT_FOUND if there's no volume, fn's nonzero result,
 * or exfat_list's errors.
 */
int card_list(exfat_read_fn read, const card_map *m, exfat_list_fn fn, void *ctx);

#endif /* CARD_H */
