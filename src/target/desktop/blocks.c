#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blocks.h"
#include "card.h"
#include "errcodes.h"
#include "sealed.h"

/*
 * The block device is one of:
 * - a block file (MODE_FILE): block n at byte n * 4096. Its 512-byte
 *   "sectors" can be read too, but none written (no exFAT volume). On
 *   Windows this can be the card's own copy: `lf -k F:\lfblocks.fb4`.
 * - a card image (MODE_CARD): a file that holds a whole card, with an
 *   exFAT volume holding lfblocks.fb4 (card.h), just as the CH32H417 sees
 *   its microSD card.
 */
enum { MODE_FILE, MODE_CARD };

#define SECTORS_PER_BLOCK   (BLK_SIZE_BYTES / BLK_SECTOR_BYTES)   /* 8 */

/* Active file path tracker */
static char active_blk_filename[256] = {0};

static int mode = MODE_FILE;
static uint32_t base = 0;               /* sector of block 0 */
static uint32_t dev_sectors = 0;        /* sectors in the file */
static card_map map;                    /* MODE_CARD: what card.c found */

/* Blocks that can be read: the whole file if it has the signature, else
   just block 0 */
static uint32_t actual_blocks = 0;

/* Block 0 starts with BLK_SIGNATURE, so the blocks can be written */
static int writable = 0;

/* A sealed file is mounted: its content blocks are checked and decrypted
   as they're read (sealed.c), and nothing can be written */
static int sealed = 0;

/* What this simulated board holds for sealed files (blk_set_board), or NULL */
static const sealed_board *board = NULL;

void blk_set_board(const sealed_board *b) {
    board = b;
}

/* Creates a blank block file: SIMNUMBLOCKS blocks of spaces, with the
   signature at the start of block 0. Returns 0 or an error code. */
static int blk_create(const char* target_file) {
    FILE* file = fopen(target_file, "wb");
    if (!file) {
        return ERR_BLK_CREATE_FAIL;
    }
    char space_buf[BLK_SIZE_BYTES];
    for (int i = 0; i < SIMNUMBLOCKS; i++) {
        memset(space_buf, 0x20, sizeof(space_buf));
        if (i == 0) {
            memcpy(space_buf, BLK_SIGNATURE, BLK_SIGNATURE_LEN);
        }
        if (fwrite(space_buf, 1, BLK_SIZE_BYTES, file) != BLK_SIZE_BYTES) {
            fclose(file);
            return ERR_BLK_WRITE_INIT;
        }
    }
    if (fclose(file) != 0) {
        return ERR_BLK_WRITE_INIT;
    }
    return 0;
}

/* Reads count sectors from lba now; bounds already checked */
static int read_now(uint32_t lba, uint32_t *dest, int count) {
    FILE *file = fopen(active_blk_filename, "rb");
    if (!file) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (fseek(file, (long)lba * BLK_SECTOR_BYTES, SEEK_SET) != 0) {
        fclose(file);
        return ERR_BLK_SEEK_FAIL;
    }
    size_t want = (size_t)count * BLK_SECTOR_BYTES;
    size_t got = fread(dest, 1, want, file);
    fclose(file);
    return (got == want) ? 0 : ERR_BLK_READ_FAIL;
}

/* Writes count sectors from lba now; protection and bounds already checked */
static int write_now(uint32_t lba, uint32_t *src, int count) {
    // Open in 'r+b' to allow selective middle updates without destroying adjacent data ranges
    FILE *file = fopen(active_blk_filename, "r+b");
    if (!file) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (fseek(file, (long)lba * BLK_SECTOR_BYTES, SEEK_SET) != 0) {
        fclose(file);
        return ERR_BLK_SEEK_FAIL;
    }
    size_t want = (size_t)count * BLK_SECTOR_BYTES;
    size_t put = fwrite(src, 1, want, file);
    fclose(file);
    return (put == want) ? 0 : ERR_BLK_WRITE_FAIL;
}

/* sealed.c's raw block reader, for the hash table blocks */
static int raw_block(uint32_t blk, uint8_t *buf) {
    return read_now(base + blk * SECTORS_PER_BLOCK, (uint32_t *)buf, SECTORS_PER_BLOCK);
}

/* If the blocks found so far (file_blocks of them, from sector base) are a
   sealed file, mounts it and offers its content, read-only; if it can't
   be mounted, offers nothing. Leaves other files alone. */
static void check_sealed(uint32_t file_blocks) {
    static uint8_t b0[BLK_SIZE_BYTES];
    if (file_blocks == 0 || raw_block(0, b0) != 0
        || memcmp(b0, SEALED_MAGIC, SEALED_MAGIC_LEN) != 0) {
        return;
    }
    char msg[120];
    writable = 0;
    actual_blocks = 0;
    if (board == NULL) {
        fprintf(stderr, "blocks: a sealed file, but lf has no board file (-K)\n");
    } else if (sealed_mount(b0, file_blocks, board, raw_block, msg, sizeof msg) == 0) {
        sealed = 1;
        actual_blocks = sealed_blocks();
        fprintf(stderr, "blocks: %s\n", msg);
    } else {
        fprintf(stderr, "blocks: %s\n", msg);
    }
}

/* card.c's sector reader, for a card image */
static int card_read(uint32_t sector, uint8_t *buf) {
    if (sector >= dev_sectors) {
        return ERR_BLK_BOUNDS;
    }
    return read_now(sector, (uint32_t *)buf, 1);
}

int blk_init(char* filename, uint32_t* capacity) {
    char* target_file = (filename && filename[0] != '\0') ? filename : BLOCKFILENAME;

    // Save target file path
    strncpy(active_blk_filename, target_file, sizeof(active_blk_filename) - 1);
    actual_blocks = 0;
    writable = 0;
    sealed = 0;
    sealed_unmount();
    mode = MODE_FILE;
    base = 0;
    dev_sectors = 0;
    memset(&map, 0, sizeof map);
    if (capacity != NULL) {
        *capacity = 0;
    }

    FILE* file = fopen(target_file, "rb");
    if (!file) {
        int ior = blk_create(target_file);
        if (ior) return ior;
        file = fopen(target_file, "rb");
        if (!file) return ERR_BLK_OPEN_FAIL;
    }

    // Check the signature and the file's length
    char signature[BLK_SIGNATURE_LEN] = {0};
    size_t got = fread(signature, 1, BLK_SIGNATURE_LEN, file);
    fseek(file, 0, SEEK_END);
    long file_len = ftell(file);
    fclose(file);
    if (file_len < 0) {
        return ERR_BLK_SEEK_FAIL;
    }
    dev_sectors = (uint32_t)(file_len / BLK_SECTOR_BYTES);

    if (got != BLK_SIGNATURE_LEN || memcmp(signature, BLK_SIGNATURE, BLK_SIGNATURE_LEN) != 0) {
        // Not a LiteForth block file: maybe a card image
        char msg[120];
        int ior = card_find_blocks(card_read, dev_sectors, &map, msg, sizeof msg);
        if (ior == 0 && map.mounted) {
            // An exFAT card: its blocks if it has lfblocks.fb4, and its
            // sectors either way, as on the board
            mode = MODE_CARD;
            base = map.base;
            actual_blocks = map.blocks;
            writable = map.writable;
            fprintf(stderr, "blocks: %s\n", msg);
            if (writable) {
                check_sealed(actual_blocks);
            }
        } else {
            // Block 0 can be read, if it's all there
            memset(&map, 0, sizeof map);
            actual_blocks = (file_len >= (long)BLK_SIZE_BYTES) ? 1 : 0;
        }
    } else {
        actual_blocks = (uint32_t)(file_len / BLK_SIZE_BYTES);
        writable = 1;
        check_sealed(actual_blocks);
        if (!writable) {                // sealed: never padded or written
            if (capacity != NULL) {
                *capacity = actual_blocks;
            }
            return 0;
        }
        // Pad the file with spaces to a whole number of blocks
        long remainder = file_len % BLK_SIZE_BYTES;
        if (remainder != 0) {
            long padding_needed = BLK_SIZE_BYTES - remainder;
            char space_buf[BLK_SIZE_BYTES];
            memset(space_buf, 0x20, sizeof(space_buf));
            file = fopen(target_file, "ab");
            if (file) {
                if (fwrite(space_buf, 1, padding_needed, file) == (size_t)padding_needed) {
                    file_len += padding_needed;
                }
                fclose(file);
            }
        }
        actual_blocks = (uint32_t)(file_len / BLK_SIZE_BYTES);
        dev_sectors = (uint32_t)(file_len / BLK_SECTOR_BYTES);
        writable = 1;
    }

    if (capacity != NULL) {
        *capacity = actual_blocks;
    }
    return 0;
}

/*=========================================================================
* Unwaited transfers: blk_sector_read_imm and blk_sector_write_imm start
* a transfer of sectors, blk_read_imm and blk_write_imm of blocks, and
* blk_status tells how it went. On the desktop they are done at once.
=========================================================================*/

static int status;                      /* blk_status: 0, an error, 1 reading, 2 writing */

/* Checks a request for length blocks from blk */
static int check_run(uint32_t blk, int length) {
    if (length < 1 || length > BLK_MAX_LENGTH) {
        return ERR_INVALID_NUMERIC;
    }
    if (blk >= actual_blocks || (uint32_t)length > actual_blocks - blk) {
        return ERR_BLK_BOUNDS;
    }
    return 0;
}

/* Checks a request for count sectors from lba */
static int check_sectors(uint32_t lba, int count) {
    if (count < 1 || count > BLK_MAX_SECTORS) {
        return ERR_INVALID_NUMERIC;
    }
    if (lba >= dev_sectors || (uint32_t)count > dev_sectors - lba) {
        return ERR_BLK_BOUNDS;
    }
    return 0;
}

int blk_write_fence(uint32_t lba, int count) {
    if (mode != MODE_CARD || !map.mounted || lba < map.vol.start
        || (uint64_t)lba + (uint32_t)count > (uint64_t)map.vol.start + map.vol.sectors) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    return 0;
}

int blk_sector_read_imm(uint32_t lba, uint32_t *dest, int count) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    int err = check_sectors(lba, count);
    if (err) {
        return err;
    }
    status = read_now(lba, dest, count);
    return 0;
}

int blk_sector_write_imm(uint32_t lba, uint32_t *src, int count) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    int err = check_sectors(lba, count);
    if (!err) {
        err = blk_write_fence(lba, count);
    }
    if (err) {
        return err;
    }
    status = write_now(lba, src, count);
    return 0;
}

int blk_read_imm(uint32_t blk, uint32_t *dest, int length) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    int err = check_run(blk, length);
    if (err) {
        return err;
    }
    status = read_now(base + blk * SECTORS_PER_BLOCK, dest, length * SECTORS_PER_BLOCK);
    for (int i = 0; sealed && status == 0 && i < length; i++) {
        status = sealed_open_block(blk + i, (uint8_t *)dest + i * BLK_SIZE_BYTES);
        if (status) {                   // leave no ciphertext behind either
            memset((uint8_t *)dest + i * BLK_SIZE_BYTES, 0, (size_t)(length - i) * BLK_SIZE_BYTES);
        }
    }
    return 0;
}

int blk_write_imm(uint32_t blk, uint32_t *src, int length) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    int err = check_run(blk, length);
    if (err) {
        return err;
    }
    status = write_now(base + blk * SECTORS_PER_BLOCK, src, length * SECTORS_PER_BLOCK);
    return 0;
}

int blk_status(void) {
    return status;
}

/* Waits for the transfer in progress, if any; returns its blk_status */
static int finish(void) {
    int s;
    while ((s = blk_status()) > 0) {
    }
    return s;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    finish();
    int err = blk_read_imm(blk, dest, 1);
    return err ? err : finish();
}

int blk_write(uint32_t blk, uint32_t *src) {
    finish();
    int err = blk_write_imm(blk, src, 1);
    return err ? err : finish();
}

int blk_sector_read(uint32_t lba, uint32_t *dest, int count) {
    finish();
    int err = blk_sector_read_imm(lba, dest, count);
    return err ? err : finish();
}
