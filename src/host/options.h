#ifndef OPTIONS_H
#define OPTIONS_H

// forth.c
#define TF_VERSION            0 // version 0.00
#define CASE_SENSITIVE        1 // is FIND case sensitive?
#define FAT_FORTH             1 // all options
#define CONTEXT_MAX          15 // depth of possible search order
#define WIDS_MAX              8 // number of different wordlists supported
#define DOT_S_MAX             8 // maximum depth to display in .s
#define TIBCELLS             21 // The size of the TIB in cells
// vm.c                     
#define VM_LOG2_PAGES         3 // log2 of the number of pages in the memory space
#define STACK_CAPACITY	    128 // Size of the data and return stacks in cells
// main.c
#define FLASH_PAGE_CELLS   1024 // Flash memory page size [1]
#define RAM_PAGE              1 // The memory page used by system variables
#define RAM_PAGE_CELLS     1024 // RAM page size

/* NOTES:
[1] All pages below page RAM_PAGE are Flash pages, which are 1 or more
    physical sectors of Flash memory. All Flash pages are the same size.
*/

/* ==========================================================================
   Mass Storage (Block) Simulation Configuration
   ========================================================================== */

/**
 * Default Mass Storage Filename
 * The name of the binary file used to simulate block storage.
 */
#define BLOCKFILENAME           "lfblocks.bin"

/**
 * Number of Mass Storage Blocks
 * Total count of standard virtual blocks available to the simulated storage system.
 */
#define SIMNUMBLOCKS            240


/* ==========================================================================
   Flash Memory Simulation Configuration
   ========================================================================== */

/**
 * Default Flash Simulation Filename
 * The fallback binary file name used by flash.c if no custom argument is provided.
 */
#define FLASHFILENAME           "lfflash.bin"

/**
 * Flash Sector Size
 * The size of a single simulated hardware sector, measured in 32-bit (uint32_t) cells.
 * Example: 1024 cells * 4 bytes/cell = 4096 bytes (4KB) per sector.
 */
#define FLASHAPPSECTORSIZE      (128*256)


#endif /* OPTIONS_H */
