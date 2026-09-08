#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blocks.h"

/* Active file path tracker */
static char active_blk_filename[256] = {0};

/* Active configuration variables synced from Block 0 text fields */
static uint32_t total_blocks_allocated = SIMNUMBLOCKS; 
static uint32_t first_write_protected_block = SIMNUMBLOCKS; 

/* Track the actual physical capacity on disk for bounds checking */
static uint32_t actual_blocks = 0;

int blk_init(char *filename) {
    FILE *file = NULL;
    char *target_file = (filename && filename[0] != '\0') ? filename : BLOCKFILENAME;
    
    // Save target file path
    strncpy(active_blk_filename, target_file, sizeof(active_blk_filename) - 1);

    // Try opening existing file for updating (r+b) to handle reads and extensions
    file = fopen(target_file, "r+b");
    if (!file) {
        // File missing: Create a new blank mass storage file filled with ASCII spaces
        file = fopen(target_file, "wb");
        if (!file) {
            return ERR_BLK_CREATE_FAIL;
        }

        // Allocate a temporary 4KB buffer filled with ASCII spaces (0x20)
        char space_buf[BLK_SIZE_BYTES];
        memset(space_buf, 0x20, sizeof(space_buf));

        // Format and copy the minimal header string into the beginning of Block 0 buffer
        char header_txt[128];
        snprintf(header_txt, sizeof(header_txt), "LITEFORTHBLK 1 %X %X %X\n", 
                 BLK_SIZE_BYTES, SIMNUMBLOCKS, SIMNUMBLOCKS);
        
        size_t header_len = strlen(header_txt);
        memcpy(space_buf, header_txt, header_len);

        // Write block 0
        if (fwrite(space_buf, 1, BLK_SIZE_BYTES, file) != BLK_SIZE_BYTES) {
            fclose(file);
            return ERR_BLK_WRITE_INIT;
        }

        // Fill remaining simulation blocks completely with raw spaces
        memset(space_buf, 0x20, sizeof(space_buf));
        for (int i = 1; i < SIMNUMBLOCKS; i++) {
            if (fwrite(space_buf, 1, BLK_SIZE_BYTES, file) != BLK_SIZE_BYTES) {
                fclose(file);
                return ERR_BLK_WRITE_INIT;
            }
        }
        fclose(file);
        
        total_blocks_allocated = SIMNUMBLOCKS;
        first_write_protected_block = SIMNUMBLOCKS;
        actual_blocks = SIMNUMBLOCKS;
        return 0;
    }

    // Determine the current file length
    fseek(file, 0, SEEK_END);
    long file_len = ftell(file);
    
    // Check if the file length is a clean multiple of 4096 (BLK_SIZE_BYTES)
    long remainder = file_len % BLK_SIZE_BYTES;
    if (remainder != 0) {
        long padding_needed = BLK_SIZE_BYTES - remainder;
        char space_buf[BLK_SIZE_BYTES];
        memset(space_buf, 0x20, sizeof(space_buf));
        
        // Append spaces to expand the file to a multiple of BLK_SIZE_BYTES
        if (fwrite(space_buf, 1, padding_needed, file) == (size_t)padding_needed) {
            file_len += padding_needed; // Update file length tracking
        }
    }

    // Save the file's actual number of blocks 
    actual_blocks = (uint32_t)(file_len / BLK_SIZE_BYTES);

    // Read Block 0 header content to parse slider positions
    fseek(file, 0, SEEK_SET);
    char block0_text[128] = {0};
    fread(block0_text, 1, sizeof(block0_text) - 1, file);
    fclose(file);

    // Verify block format properties
    char signature[32] = {0};
    int version = 0;
    uint32_t parsed_blk_size = 0;
    uint32_t parsed_total_blks = 0;
    uint32_t parsed_wp_start = 0;

    int scanned = sscanf(block0_text, "%31s %d %x %x %x", 
                         signature, &version, &parsed_blk_size, &parsed_total_blks, &parsed_wp_start);

    if (scanned < 5 || strcmp(signature, "LITEFORTHBLK") != 0) {
        return ERR_BLK_PARSE_FAIL;
    }

    // Cache the settings into active runtime properties
    total_blocks_allocated = parsed_total_blks;
    first_write_protected_block = parsed_wp_start;

    return 0;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    // Bounds check against actual_blocks instead of header values
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
    // Bounds check against actual_blocks instead of header values
    if (blk >= actual_blocks) {
        return ERR_BLK_BOUNDS;
    }

    // Enforce write protection rule: Reject writes if block index is >= the slider threshold
    if (blk >= first_write_protected_block) {
        return ERR_BLK_WRITE_PROTECTED;
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

    // Special handling: if block 0 itself is overwritten, re-sync runtime write-protect properties
    if (blk == 0) {
        char *block0_ptr = (char *)src;
        char signature[32] = {0};
        int version = 0;
        uint32_t parsed_blk_size = 0;
        uint32_t parsed_total_blks = 0;
        uint32_t parsed_wp_start = 0;

        int scanned = sscanf(block0_ptr, "%31s %d %x %x %x", 
                             signature, &version, &parsed_blk_size, &parsed_total_blks, &parsed_wp_start);
        
        if (scanned == 5 && strcmp(signature, "LITEFORTHBLK") == 0) {
            total_blocks_allocated = parsed_total_blks;
            first_write_protected_block = parsed_wp_start;
        }
    }

    return 0;
}
