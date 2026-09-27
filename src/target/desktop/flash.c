#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flash.h"
#include "errcodes.h"
#include "options.h"

#define FLASH_CELLS (FLASH_PAGE_CELLS * RAM_PAGE)
int32_t flashmem[FLASH_CELLS];

static char current_filename[256] = { 0 };

/* Safe cross-platform fopen helper */
static inline FILE* safe_fopen(const char* filename, const char* mode) {
#if defined(_MSC_VER) || defined(__STDC_LIB_EXT1__)
    FILE* file = NULL;
    if (fopen_s(&file, filename, mode) != 0) {
        return NULL;
    }
    return file;
#else
    return fopen(filename, mode);
#endif
}

int flash_init(char* filename, int32_t** mem) {
    *mem = flashmem;
    FILE* file = NULL;
    char* target_file = (filename && filename[0] != '\0') ? filename : FLASHFILENAME;

    // Safely copy the filename, ensuring null-termination
    snprintf(current_filename, sizeof(current_filename), "%s", target_file);

    // Try to open existing file for reading
    file = safe_fopen(target_file, "rb");
    if (!file) {
        // File doesn't exist, create a blank hardware state (0xFF)
        memset(flashmem, 0xFF, sizeof(flashmem));

        file = safe_fopen(target_file, "wb");
        if (!file) {
            return ERR_FLASH_CREATE_FAIL;
        }

        size_t written = fwrite(flashmem, sizeof(uint32_t), FLASH_CELLS, file);
        fclose(file);

        if (written != FLASH_CELLS) {
            return ERR_FLASH_WRITE_INIT;
        }
        return 0;
    }

    // Read existing file into flash memory array
    size_t read_cells = fread(flashmem, sizeof(uint32_t), FLASH_CELLS, file);
    fclose(file);

    // If the file was smaller than expected, ensure the remaining region defaults to 0xFF
    if (read_cells < FLASH_CELLS) {
        size_t remaining = FLASH_CELLS - read_cells;
        memset(&flashmem[read_cells], 0xFF, remaining * sizeof(uint32_t));
    }

    return 0;
}

int flash_program(uint32_t* m, int page) {
    // Bounds check the target sector
    if (page < 0 || page >= RAM_PAGE) {
        return ERR_FLASH_INVALID_SECTOR;
    }

    // Update in-memory flashmem array to stay synchronized
    size_t start_index = (size_t)page * FLASH_PAGE_CELLS;
    memcpy(&flashmem[start_index], m, FLASH_PAGE_CELLS * sizeof(uint32_t));

    // Safety fallback if flash_init wasn't called or filename is missing
    char* target_file = (current_filename[0] != '\0') ? current_filename : FLASHFILENAME;

    // Open file in "r+b" mode to overwrite the specific segment without truncating
    FILE* file = safe_fopen(target_file, "r+b");
    if (!file) {
        return ERR_FLASH_OPEN_WRITE;
    }

    // Seek to the start byte of the target sector
    size_t offset = (size_t)(page * FLASH_PAGE_CELLS * sizeof(uint32_t));
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return ERR_FLASH_SEEK_FAIL;
    }

    // Write the updated segment to disk
    size_t written = fwrite(m, sizeof(uint32_t), FLASH_PAGE_CELLS, file);
    fclose(file);

    if (written != FLASH_PAGE_CELLS) {
        return ERR_FLASH_WRITE_SECTOR;
    }

    return 0;
}
int flash_rndkey(void) {
    // Desktop simulation stub: do nothing
    return 0;
}
