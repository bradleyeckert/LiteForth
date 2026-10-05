/*
 * Block mass storage for the CH32H417 (V5F): none yet. LiteForth sees a
 * device with 0 blocks, so BLOCK and LOAD fail with ERR_BLK_BOUNDS.
 */
#include "blocks.h"
#include "../../../../errcodes.h"   /* src/errcodes.h */

int blk_init(char *filename, uint32_t *capacity) {
    (void)filename;
    if (capacity) *capacity = 0;
    return 0;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    (void)blk;
    (void)dest;
    return ERR_BLK_BOUNDS;
}

int blk_write(uint32_t blk, uint32_t *src) {
    (void)blk;
    (void)src;
    return ERR_BLK_BOUNDS;
}
