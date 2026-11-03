#ifndef OPTIONS_H
#define OPTIONS_H

/* LiteForth options for the ESP32-C5 (ESP-IDF application). Same settings as
   src/target/desktop/options.h except where marked ESP32C5. Milestone 1
   (REPL over the CH340 UART + flash); see doc/ESP32C5/esp32c5-notes.md. */

// forth.c
#define TF_VERSION            2 // version x.xx
#define CASE_INSENSITIVE      1 // is FIND case-insensitive?
#define FAT_FORTH             1 // all options
#define CONTEXT_SIZE         12 // depth of possible search order
#define WIDS_MAX              8 // number of different wordlists supported
#define DOT_S_MAX             8 // maximum depth to display in .s
#define TIBCELLS      (132 / 4) // The size of the TIB in cells
#define MAX_LOAD_NESTING      8 // deepest you can nest LOADs
#define SCREEN_COLUMNS      128 // columns per screen
#define VM_STEP_LIMIT  100000000 // Max VM steps between `break` instructions
// vm.c
#define VM_LOG2_PAGES         3 // log2 of the number of pages in the memory space
#define STACK_CAPACITY      128 // Data and return stack size in cells: a power of 2, at least 32
// ESP32C5: use the portable C vmExec (vm.c) for milestone 1. Define VM_ASM
// later to switch to src/vm_rv32.S once it is confirmed to assemble under
// riscv32-esp-elf (the C5 core is RV32IMAC, like the CH32H417's).
// #define VM_ASM
// main.c
#define FLASH_PAGE_CELLS  16384 // Flash memory page size [1] ESP32C5: 64KB (16 x 4KB SPI sectors)
#define RAM_PAGE              4 // The memory page used by system variables: 4 flash pages, 256KB
#define RAM_PAGE_CELLS     4096 // RAM page size [2]
#define _UDATA0     (LF_HERE0 + 0x400)      // start of UDATA space
#define _TEXT0      (FLASH_PAGE_CELLS / 2)  // start of TEXT space
// ESP32C5: no VM_IO_PAGE yet -- Forth does not reach peripheral registers
// directly in milestone 1 (level 0, IDF drivers do the hardware).
// memalloc.c
#define POOL_CAPACITY     24576 // cells of the system memory pool, 96KB [3] ESP32C5: internal SRAM for M1
#define TEMP_HEAD_CELLS    2048 // pool buffer for temporary headers (head-) [3]
// ESP32C5: no POOL_SECTION -- the pool is a plain static array in internal
// RAM (.bss) for M1. Move it to PSRAM (8MB) for a larger pool later.
#define FLASH_SHADOW_PAGES    0 // ESP32C5: flash is cache-mapped (XIP), reads are fast; no RAM shadow
// flash.c
#define FLASHFILENAME     "lfflash"       // ESP32C5: the data partition, not a file
// blocks.c: no block storage yet (milestone 3, SD over SPI)
#define BLOCKFILENAME     "lfblocks.fb4"
#define BLOCK_SIZE_CELLS   1024 // block size in cells
#define SYSTEM_BLOCKS         2 // number of block buffers in the system
#define SIMNUMBLOCKS          0 // ESP32C5: no blocks yet

/* NOTES (see src/target/CH32H417/V5F/User/options.h for the full text):
[1] Pages below RAM_PAGE are flash pages, one or more physical sectors each,
    all the same size. On the C5 the SPI flash erase sector is 4KB, so a
    64KB page is 16 sectors; flash.c erases the whole page's range at once.
[2] RAM_PAGE_CELLS must hold the block buffers, system variables and app data.
[3] open-flash allocates a FLASH_PAGE_CELLS cache from the pool while a flash
    page is open, on top of the RAM page; head- allocates TEMP_HEAD_CELLS. */

#if (RAM_PAGE_CELLS < (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS + 0x400))
#error "RAM_PAGE_CELLS is too small, choose a higher number."
#endif

#if (POOL_CAPACITY < RAM_PAGE_CELLS + FLASH_PAGE_CELLS + TEMP_HEAD_CELLS + 3)
#error "POOL_CAPACITY must hold the RAM page, an open flash page and the temporary headers."
#endif

#if ((FLASH_PAGE_CELLS * 4) % 0x1000)
#error "ESP32C5: a flash page must be a whole number of 4KB SPI flash sectors."
#endif

#if (RAM_PAGE >= (1 << VM_LOG2_PAGES))
#error "RAM_PAGE must be a valid page number."
#endif

#endif /* OPTIONS_H */
