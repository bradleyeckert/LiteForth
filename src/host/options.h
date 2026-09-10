#ifndef OPTIONS_H
#define OPTIONS_H

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
