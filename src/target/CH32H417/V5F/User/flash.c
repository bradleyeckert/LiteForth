/*
 * LiteForth flash pages in the CH32H417's code flash (V5F).
 *
 * The 960KB code flash (480KB in single-flash mode) is mapped at
 * 0x08000000. The V3F's image takes the first 64KB and the V5F's the next
 * 128KB; Link_v5f.ld reserves the 256KB after that for LiteForth and
 * defines __lf_flash_start and __lf_flash_size. The CPU reads the pages in
 * place, as the desktop build reads its flash array.
 *
 * flash_program uses WCH's FLASH_ROM_ERASE and FLASH_ROM_WRITE (fast mode:
 * 8KB sectors or 64KB blocks, 256-byte program pages), which unlock and
 * re-lock the flash themselves. Both cores run their code from RAM (ITCM /
 * RAM_CODE), so neither stalls on instruction fetch while the flash is busy.
 */
#include <string.h>
#include "ch32h417.h"
#include "flash.h"
#include "options.h"
#include "../../../../errcodes.h"   /* src/errcodes.h */

/* From SRC/Ld/V5F/Link_v5f.ld. Their addresses are the values. */
extern const uint32_t __lf_flash_start[];
extern const uint8_t  __lf_flash_size[];

#define PAGE_BYTES  (FLASH_PAGE_CELLS * sizeof(uint32_t))

int flash_init(char *filename, int32_t** mem) {
    (void)filename;
    *mem = (int32_t *)__lf_flash_start;
    if ((uint32_t)RAM_PAGE * PAGE_BYTES > (uint32_t)__lf_flash_size) {
        return ERR_FLASH_INVALID_SECTOR; /* options.h and the linker disagree */
    }
    return 0;
}

int flash_program(uint32_t *m, int page) {
    if (page < 0 || page >= RAM_PAGE) {
        return ERR_FLASH_INVALID_SECTOR;
    }
    uint32_t addr = (uint32_t)__lf_flash_start + (uint32_t)page * PAGE_BYTES;
    const void *dst = (const void *)addr;

    if (memcmp(dst, m, PAGE_BYTES) == 0) {
        return 0;                       /* unchanged: don't wear the flash */
    }
    if (FLASH_ROM_ERASE(addr, PAGE_BYTES) != FLASH_COMPLETE) {
        return ERR_FLASH_WRITE_SECTOR;
    }
    if (FLASH_ROM_WRITE(addr, m, PAGE_BYTES) != FLASH_COMPLETE) {
        return ERR_FLASH_WRITE_SECTOR;
    }
    if (memcmp(dst, m, PAGE_BYTES) != 0) {
        return ERR_FLASH_WRITE_SECTOR;  /* didn't take */
    }
    return 0;
}

int flash_rndkey(void) {
    return 0;
}
