#ifndef OPTIONS_H
#define OPTIONS_H

// forth.c
#define TF_VERSION            1 // version x.xx
#define CASE_INSENSITIVE      1 // is FIND case-insensitive?
#define FAT_FORTH             1 // all options
#define CONTEXT_MAX          15 // depth of possible search order
#define WIDS_MAX              8 // number of different wordlists supported
#define DOT_S_MAX             8 // maximum depth to display in .s
#define TIBCELLS             21 // The size of the TIB in cells
#define MAX_INPUT_STACK       8 // deepest you can nest blocks
// vm.c                     
#define VM_LOG2_PAGES         3 // log2 of the number of pages in the memory space
#define STACK_CAPACITY	    128 // Size of the data and return stacks in cells
// main.c
#define FLASH_PAGE_CELLS   1024 // Flash memory page size [1]
#define RAM_PAGE              1 // The memory page used by system variables
#define RAM_PAGE_CELLS     8192 // RAM page size [2]
// memalloc.c
#define POOL_CAPACITY     16384 // cells of the main memory pool
// flash.c
#define FLASHFILENAME     "lfflash.bin"
// blocks.c
#define BLOCKFILENAME     "lfblocks.bin"
#define BLOCK_SIZE_CELLS   1024 // block size in cells
#define SYSTEM_BLOCKS         4 // number of block buffers in the system
#define SIMNUMBLOCKS        256 // number of blocks in simulated block system

/* NOTES:
[1] All pages below page RAM_PAGE are Flash pages, which are 1 or more
    physical sectors of Flash memory. All Flash pages are the same size.

[2] RAM_PAGE_CELLS must be at least enough for the block buffers, system
    variables, and application data.
*/

#if (RAM_PAGE_CELLS < (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS + 0x400))
#error "RAM_PAGE_CELLS is too small, choose a higher number."
#endif

#if (POOL_CAPACITY < RAM_PAGE_CELLS)
#error "POOL_CAPACITY is too small, choose a higher number."
#endif

#endif /* OPTIONS_H */
