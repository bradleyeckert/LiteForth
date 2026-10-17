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
 * can be read.
 */

#define CARD_BLOCKS_FILE    "lfblocks.fb4"

enum { CARD_NONE, CARD_EXFAT_FILE };

typedef struct {
    int kind;                   /* CARD_NONE or CARD_EXFAT_FILE */
    uint32_t base;              /* card sector of block 0 */
    uint32_t blocks;            /* blocks that can be read */
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

#endif /* CARD_H */
