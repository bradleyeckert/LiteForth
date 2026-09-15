#ifndef OPTIONS_H
#define OPTIONS_H

// forth.c
#define TF_VERSION       0  // version 0.00
#define CONTEXT_MAX      8  // depth of possible search order
#define WIDS_MAX         8  // number of different wordlists supported
#define RAM_PAGE         1  // The memory page used by system variables
#define TIBCELLS        21  // The size of the TIB in cells
#define CASE_SENSITIVE   1  // is FIND case sensitive?
#define CR_IS_CRLF       1  // 0 if Unix style line endings
#define DOT_S_MAX        8  // maximum depth to display in .s
#define FAT_FORTH        1  // all options
// vm.c                   
#define VM_SEGMENT_BITS  3  // log2 of the number of pages in the memory space
#define STACK_CAPACITY  64  // Size of the data and return stacks in cells


/* ==========================================================================
   LiteForth Virtual Machine Memory Configuration
   ========================================================================== */

/** 
 * VM Data RAM Size
 * Number of 32-bit cells allocated for the virtual machine's RAM region.
 */
#define RAMSIZE                 16384

/** 
 * VM Data ROM Size
 * Number of 32-bit cells allocated for the virtual machine's ROM/dictionary region.
 */
#define ROMSIZE                 32768


/* ==========================================================================
   Mass Storage (Block) Simulation Configuration
   ========================================================================== */

/**
 * Default Mass Storage Filename
 * The name of the binary file used to simulate block storage.
 */
#define BLOCKFILENAME           "forth_blocks.bin"

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
#define FLASHFILENAME           "flash_sim.bin"

/**
 * Flash Sector Size
 * The size of a single simulated hardware sector, measured in 32-bit (uint32_t) cells.
 * Example: 1024 cells * 4 bytes/cell = 4096 bytes (4KB) per sector.
 */
#define FLASHAPPSECTORSIZE      (128*256)

/**
 * Number of Application Flash Sectors
 * The total count of sectors mapped into the application region of the flash array.
 */
#define FLASHAPPSECTORS         2

#endif /* OPTIONS_H */
