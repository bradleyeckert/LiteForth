#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blocks.h"
#include "errcodes.h"

/* Active file path tracker */
static char active_blk_filename[256] = {0};

/* Blocks that can be read: the whole file if it has the signature, else
   just block 0 */
static uint32_t actual_blocks = 0;

/* The file starts with BLK_SIGNATURE, so its blocks can be written */
static int writable = 0;

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

int blk_init(char* filename, uint32_t* capacity) {
    char* target_file = (filename && filename[0] != '\0') ? filename : BLOCKFILENAME;

    // Save target file path
    strncpy(active_blk_filename, target_file, sizeof(active_blk_filename) - 1);
    actual_blocks = 0;
    writable = 0;
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

    if (got != BLK_SIGNATURE_LEN || memcmp(signature, BLK_SIGNATURE, BLK_SIGNATURE_LEN) != 0) {
        // Not a LiteForth block file: block 0 can be read, if it's all there
        actual_blocks = (file_len >= (long)BLK_SIZE_BYTES) ? 1 : 0;
    } else {
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
        writable = 1;
    }

    if (capacity != NULL) {
        *capacity = actual_blocks;
    }
    return 0;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    if (blk >= actual_blocks) {
        return ERR_BLK_BOUNDS;
    }

    char *target_file = (active_blk_filename[0] != '\0') ? active_blk_filename : BLOCKFILENAME;
    FILE *file = fopen(target_file, "rb");
    if (!file) {
        return ERR_BLK_OPEN_FAIL;
    }

    long offset = (long)(blk * BLK_SIZE_BYTES);
    if (fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return ERR_BLK_SEEK_FAIL;
    }

    size_t read_bytes = fread(dest, 1, BLK_SIZE_BYTES, file);
    fclose(file);

    if (read_bytes != BLK_SIZE_BYTES) {
        return ERR_BLK_READ_FAIL;
    }

    return 0;
}

int blk_write(uint32_t blk, uint32_t *src) {
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;   // not a LiteForth block file
    }
    if (blk >= actual_blocks) {
        return ERR_BLK_BOUNDS;
    }

    char *target_file = (active_blk_filename[0] != '\0') ? active_blk_filename : BLOCKFILENAME;

    // Open in 'r+b' to allow selective middle updates without destroying adjacent data ranges
    FILE *file = fopen(target_file, "r+b");
    if (!file) {
        return ERR_BLK_OPEN_FAIL;
    }

    long offset = (long)(blk * BLK_SIZE_BYTES);
    if (fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return ERR_BLK_SEEK_FAIL;
    }

    size_t written_bytes = fwrite(src, 1, BLK_SIZE_BYTES, file);
    fclose(file);

    if (written_bytes != BLK_SIZE_BYTES) {
        return ERR_BLK_WRITE_FAIL;
    }

    return 0;
}
