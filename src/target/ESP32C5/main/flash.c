/*
 * LiteForth flash pages in a dedicated ESP32-C5 flash partition ("lfflash").
 *
 * The external SPI flash is memory-mapped through the cache, so the VM reads
 * the pages in place at the partition's mapped address (esp_partition_mmap) --
 * the same way the CH32H417 reads its internal flash directly, and the
 * desktop reads its flash array. flash_program erases and writes a whole
 * page with esp_partition_erase_range / esp_partition_write, which handle the
 * cache internally. This is NOT the app partition (the running code); it is a
 * separate data partition, so programming it never touches the image that is
 * executing. See doc/ESP32C5/esp32c5-notes.md.
 */
#include <string.h>
#include "flash.h"
#include "options.h"
#include "../../../errcodes.h"   /* src/errcodes.h */
#include "esp_partition.h"

#define PAGE_BYTES  (FLASH_PAGE_CELLS * sizeof(uint32_t))
#define LF_FLASH_BYTES ((size_t)RAM_PAGE * PAGE_BYTES)

static const esp_partition_t *s_part;
static const uint32_t *s_mapped;            /* cache-mapped, read-only */
static esp_partition_mmap_handle_t s_map;
static int s_locked;

int flash_init(char *filename, uint32_t** mem) {
    const char *name = (filename && filename[0]) ? filename : FLASHFILENAME;
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY, name);
    if (s_part == NULL) {
        return ERR_FLASH_CREATE_FAIL;       /* no lfflash partition in the table */
    }
    if (s_part->size < LF_FLASH_BYTES) {
        return ERR_FLASH_INVALID_SECTOR;    /* partition smaller than RAM_PAGE pages */
    }
    const void *ptr = NULL;
    if (esp_partition_mmap(s_part, 0, LF_FLASH_BYTES, ESP_PARTITION_MMAP_DATA,
                           &ptr, &s_map) != ESP_OK) {
        return ERR_FLASH_CREATE_FAIL;
    }
    s_mapped = (const uint32_t *)ptr;
    *mem = (uint32_t *)s_mapped;            /* the VM reads flash in place */
    return 0;
}

uint32_t* flash_page(int page) {
    if (page < 0 || page >= RAM_PAGE || s_mapped == NULL) {
        return NULL;
    }
    return (uint32_t *)s_mapped + (size_t)page * FLASH_PAGE_CELLS;
}

int flash_program(uint32_t *m, int page) {
    if (page < 0 || page >= RAM_PAGE) {
        return ERR_FLASH_INVALID_SECTOR;
    }
    size_t off = (size_t)page * PAGE_BYTES;
    if (memcmp((const uint8_t *)s_mapped + off, m, PAGE_BYTES) == 0) {
        return 0;                           /* unchanged: don't wear the flash */
    }
    if (esp_partition_erase_range(s_part, off, PAGE_BYTES) != ESP_OK) {
        return ERR_FLASH_OPEN_WRITE;
    }
    if (esp_partition_write(s_part, off, m, PAGE_BYTES) != ESP_OK) {
        return ERR_FLASH_OPEN_WRITE;
    }
    return 0;                               /* the write invalidates the cache */
}

int flash_locked(void) {
    return s_locked;                        /* 0 at SECURITY_LEVEL 0 */
}

void flash_set_locked(int locked) {
    s_locked = locked;
}

int flash_rndkey(void) {
    return 0;                               /* no RoT key sector yet (level 0) */
}
