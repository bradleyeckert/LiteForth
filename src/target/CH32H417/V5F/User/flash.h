#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/* What an erased flash cell reads as. WCH's flash doesn't read erased
   cells as all ones: on the CH32H417 they read 0xE339E339. */
#define FLASH_ERASED_WORD   0xE339E339u

/*
 * LiteForth's flash pages in the CH32H417's code flash, for the V5F.
 *
 * Same interface as src/target/desktop/flash.h. The pages are
 * RAM_PAGE pages of FLASH_PAGE_CELLS cells, starting at __lf_flash_start
 * (0x08030000, defined in SRC/Ld/V5F/Link_v5f.ld), 256KB in all.
 * flash_program erases and rewrites one page. The CPU reads pages from
 * FLASH_SHADOW_PAGES on in place; the ones below that from RAM copies in
 * DTCM (see flash_page), since flash reads cost wait states.
 */

/**
 * Points mem at the first flash page, and copies the first
 * FLASH_SHADOW_PAGES pages to their RAM copies. Flash is memory-mapped, so
 * nothing is loaded or created: an erased page reads as FLASH_ERASED_WORD.
 * filename: ignored.
 * mem: receives a pointer to flash page 0 (the pages are contiguous).
 * Returns 0, or ERR_FLASH_INVALID_SECTOR if the pages don't fit the
 * flash that Link_v5f.ld reserves.
 */
int flash_init(char *filename, uint32_t** mem);

/**
 * The memory the VM should read flash page `page` from: its RAM copy if
 * page < FLASH_SHADOW_PAGES, else the flash itself. Call after flash_init.
 * Returns NULL if page is not 0 to RAM_PAGE-1.
 */
uint32_t* flash_page(int page);

/**
 * Copies FLASH_PAGE_CELLS elements from source buffer 'm' into flash page
 * 'page': erases the page, programs it and checks it. Does nothing if the
 * page already holds those contents, which saves an erase cycle when a page
 * was opened but not changed. Takes roughly as long as erasing the page,
 * and the CPU doing it stalls for that time. Then refreshes the page's RAM
 * copy, if it has one, from the flash, so the copy matches the flash even
 * if programming failed.
 * Returns 0 if okay, ERR_FLASH_INVALID_SECTOR if page is not 0 to
 * RAM_PAGE-1, or ERR_FLASH_WRITE_SECTOR if the erase or write failed or the
 * page reads back different.
 */
int flash_program(uint32_t *m, int page);

/**
 * Placeholder for filling in a random key at the end of the Root of Trust
 * (RoT) sector. Not implemented on the CH32H417 yet.
 * Returns 0.
 */
int flash_rndkey(void);

/**
 * Nonzero if LiteForth's flash pages can't be written here: on the
 * CH32H417 at SECURITY_LEVEL 2 (SEC_LOCKED_PAGES), where only update
 * packages write them and stage 1 checks them at every boot. open-flash
 * refuses then (ERR_WRITE_PROTECTED), and so does pmp.c's flash service.
 */
int flash_locked(void);

#endif /* FLASH_H */
