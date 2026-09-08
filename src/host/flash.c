#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flash.h"

/* Define global flash memory */
uint32_t flashmem[FLASHAPPSECTORSIZE * FLASHAPPSECTORS];

/* Local helper to track the currently active file path */
static char current_filename[256] = {0};

int flash_init(char *filename) {
    FILE *file = NULL;
    char *target_file = (filename && filename[0] != '\0') ? filename : FLASHFILENAME;
    
    // Save the filename for subsequent flash_sector operations
    strncpy(current_filename, target_file, sizeof(current_filename) - 1);

    // Try to open existing file for reading
    file = fopen(target_file, "rb");
    if (!file) {
        // File doesn't exist, create a blank hardware state (0xFF)
        memset(flashmem, 0xFF, sizeof(flashmem));
        
        file = fopen(target_file, "wb");
        if (!file) {
            return ERR_FLASH_CREATE_FAIL;
        }
        
        size_t written = fwrite(flashmem, sizeof(uint32_t), FLASHAPPSECTORSIZE * FLASHAPPSECTORS, file);
        fclose(file);
        
        if (written != (FLASHAPPSECTORSIZE * FLASHAPPSECTORS)) {
            return ERR_FLASH_WRITE_INIT;
        }
        return 0;
    }

    // Read existing file into flash memory array
    size_t read_cells = fread(flashmem, sizeof(uint32_t), FLASHAPPSECTORSIZE * FLASHAPPSECTORS, file);
    fclose(file);

    // If the file was smaller than expected, ensure the remaining region defaults to 0xFF
    if (read_cells < (FLASHAPPSECTORSIZE * FLASHAPPSECTORS)) {
        size_t remaining = (FLASHAPPSECTORSIZE * FLASHAPPSECTORS) - read_cells;
        memset(&flashmem[read_cells], 0xFF, remaining * sizeof(uint32_t));
    }

    return 0;
}

int flash_sector(uint32_t *m, int sector) {
    // Bounds check the target sector
    if (sector < 0 || sector >= FLASHAPPSECTORS) {
        return ERR_FLASH_INVALID_SECTOR;
    }

    // Safety fallback if flash_init wasn't called or filename is missing
    char *target_file = (current_filename[0] != '\0') ? current_filename : FLASHFILENAME;

    // 1. Copy source buffer 'm' into the global array segment
    uint32_t *dest_sector = &flashmem[sector * FLASHAPPSECTORSIZE];
    memcpy(dest_sector, m, FLASHAPPSECTORSIZE * sizeof(uint32_t));

    // 2. Open file in "r+b" mode to overwrite the specific segment without truncating
    FILE *file = fopen(target_file, "r+b");
    if (!file) {
        return ERR_FLASH_OPEN_WRITE;
    }

    // Seek to the start byte of the target sector
    long offset = (long)(sector * FLASHAPPSECTORSIZE * sizeof(uint32_t));
    if (fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return ERR_FLASH_SEEK_FAIL;
    }

    // Write the updated segment to disk
    size_t written = fwrite(dest_sector, sizeof(uint32_t), FLASHAPPSECTORSIZE, file);
    fclose(file);

    if (written != FLASHAPPSECTORSIZE) {
        return ERR_FLASH_WRITE_SECTOR;
    }

    return 0;
}

int flash_rndkey(void) {
    // Desktop simulation stub: do nothing
    return 0;
}
