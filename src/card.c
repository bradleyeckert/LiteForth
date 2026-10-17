/*
 * Finding the block space on a card: lfblocks.fb4 on exFAT. See card.h.
 */
#include <stdio.h>
#include <string.h>
#include "card.h"
#include "blocks.h"
#include "errcodes.h"

#define SECTORS_PER_BLOCK   (BLK_SIZE_BYTES / EXFAT_SECTOR_BYTES)  /* 8 */

static uint32_t sector_buf[EXFAT_SECTOR_BYTES / 4];

/* Block 0 at `base`: blocks and writable from its signature; 0 or an error */
static int check_signature(exfat_read_fn read, card_map *m, uint32_t blocks) {
    int err = read(m->base, (uint8_t *)sector_buf);
    if (err) {
        return err;
    }
    if (memcmp(sector_buf, BLK_SIGNATURE, BLK_SIGNATURE_LEN) == 0) {
        m->blocks = blocks;
        m->writable = 1;
    } else {
        m->blocks = 1;                  /* block 0, read-only, to look at */
        m->writable = 0;
    }
    return 0;
}

static int fits(uint32_t card_sectors, uint32_t base, uint32_t blocks) {
    return card_sectors == 0
        || ((uint64_t)base + (uint64_t)blocks * SECTORS_PER_BLOCK <= card_sectors);
}

int card_find_blocks(exfat_read_fn read, uint32_t card_sectors, card_map *m,
                     char *msg, int msg_len) {
    const char *why = "";
    int err;
    memset(m, 0, sizeof *m);

    if (exfat_mount(read, &m->vol) == 0) {
        m->mounted = 1;
        err = exfat_find(read, &m->vol, m->vol.root, CARD_BLOCKS_FILE, &m->file);
        if (err == 0) {
            uint64_t n = m->file.valid / BLK_SIZE_BYTES;
            if (n > 0xFFFFFFFFu / SECTORS_PER_BLOCK) {
                n = 0xFFFFFFFFu / SECTORS_PER_BLOCK;
            }
            if (m->file.flags & EXFAT_DIRECTORY) {
                why = CARD_BLOCKS_FILE " is a directory";
            } else if (!(m->file.flags & EXFAT_CONTIGUOUS)) {
                why = CARD_BLOCKS_FILE " is fragmented: copy it to the card again";
            } else if (n == 0) {
                why = CARD_BLOCKS_FILE " is shorter than one block";
            } else if (!fits(card_sectors, m->file.first_sector, (uint32_t)n)) {
                why = CARD_BLOCKS_FILE " runs past the end of the card";
            } else {
                m->kind = CARD_EXFAT_FILE;
                m->base = m->file.first_sector;
                err = check_signature(read, m, (uint32_t)n);
                if (err) {
                    return err;
                }
                snprintf(msg, msg_len, CARD_BLOCKS_FILE ", %lu blocks at sector %lu (exFAT)%s",
                         (unsigned long)m->blocks, (unsigned long)m->base,
                         m->writable ? "" : ": block 0 doesn't start with " BLK_SIGNATURE ", read-only");
                return 0;
            }
        } else if (err != ERR_FILE_NOT_FOUND) {
            why = "the exFAT root directory can't be read";
        } else {
            why = "no " CARD_BLOCKS_FILE " in the exFAT root directory";
        }
    } else {
        why = "no exFAT volume";
    }

    snprintf(msg, msg_len, "%s", why);
    return 0;
}
