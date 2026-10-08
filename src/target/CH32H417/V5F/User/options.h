#ifndef OPTIONS_H
#define OPTIONS_H

/* LiteForth options for the CH32H417's V5F core. Same settings as
   src/target/desktop/options.h except where marked CH32H417. */

// forth.c
#define TF_VERSION            2 // version x.xx
#define CASE_INSENSITIVE      1 // is FIND case-insensitive?
#define FAT_FORTH             1 // all options
#define CONTEXT_MAX          15 // depth of possible search order
#define WIDS_MAX              8 // number of different wordlists supported
#define DOT_S_MAX             8 // maximum depth to display in .s
#define TIBCELLS      (132 / 4) // The size of the TIB in cells
#define MAX_LOAD_NESTING      8 // deepest you can nest LOADs
#define SCREEN_COLUMNS      128 // columns per screen
#define VM_STEP_LIMIT  100000000 // Max VM steps between `break` instructions
// vm.c                     
#define VM_LOG2_PAGES         3 // log2 of the number of pages in the memory space
#define STACK_CAPACITY	    128 // Data and return stack size in cells: a power of 2, at least 32
// main.c
#define FLASH_PAGE_CELLS  16384 // Flash memory page size [1] CH32H417: 64KB, one erase block
#define RAM_PAGE              4 // The memory page used by system variables. CH32H417: 4 flash pages, 256KB
#define RAM_PAGE_CELLS     4096 // RAM page size [2]
#define VM_IO_PAGE            5 // CH32H417: the peripheral registers [4]
#define IO_BASE      0x40000000u // CH32H417: PERIPH_BASE in ch32h417.h
#define IO_CELLS  (0x38400u / 4) // CH32H417: the peripheral block, through UHSIF
// memalloc.c
#define POOL_CAPACITY     24576 // cells of the system memory pool. CH32H417: RAM page + open-flash cache [3]
// flash.c: the flash lives at __lf_flash_start (V5F linker script). Unused.
#define FLASHFILENAME     "lfflash.bin"
// blocks.c: no block storage yet, so these only size the buffers
#define BLOCKFILENAME     "lfblocks.bin"
#define BLOCK_SIZE_CELLS   1024 // block size in cells
#define SYSTEM_BLOCKS         2 // number of block buffers in the system
#define SIMNUMBLOCKS          0 // CH32H417: no blocks

/* NOTES:
[1] All pages below page RAM_PAGE are Flash pages, which are 1 or more
    physical sectors of Flash memory. All Flash pages are the same size.

[2] RAM_PAGE_CELLS must be at least enough for the block buffers, system
    variables, and application data.

[3] open-flash allocates a FLASH_PAGE_CELLS cache from the pool while a
    flash page is open, on top of the RAM page. The pool is a static array
    in the V5F's 256KB DTCM.

[4] main.c maps page VM_IO_PAGE onto the peripheral registers: the register
    at IO_BASE + 4c is at VM address (VM_IO_PAGE << 19) + c, so GPIOA
    (0x40010800) is 0x284200. Reads and writes are whole 32-bit words, and
    nothing checks for the gaps between peripherals.

CH32H417: flash pages 0..RAM_PAGE-1 are FLASH_PAGE_CELLS*4 bytes each,
starting at __lf_flash_start. flash.c erases and programs a whole page at
a time, so a page must be a whole number of 8KB sectors (the erase unit in
dual-flash mode; 4KB in single mode). 64KB pages erase as single blocks.
*/

#if (RAM_PAGE_CELLS < (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS + 0x400))
#error "RAM_PAGE_CELLS is too small, choose a higher number."
#endif

#if (POOL_CAPACITY < RAM_PAGE_CELLS + FLASH_PAGE_CELLS)
#error "POOL_CAPACITY must hold the RAM page and an open flash page."
#endif

#if ((FLASH_PAGE_CELLS * 4) % 0x2000)
#error "CH32H417: a flash page must be a whole number of 8KB flash sectors."
#endif

#if (RAM_PAGE * FLASH_PAGE_CELLS * 4 > 256 * 1024)
#error "CH32H417: the flash pages must fit in the 256KB reserved in Link_v5f.ld."
#endif

#if (RAM_PAGE >= (1 << VM_LOG2_PAGES))
#error "RAM_PAGE must be a valid page number."
#endif

#endif /* OPTIONS_H */
