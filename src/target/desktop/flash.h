#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/**
 * Initializes the flash simulation for reading.
 * Checks for file existence; if missing, creates a blank file filled with 0xFF.
 * Loads the file contents into the simulated flash array; any part the file
 * does not cover reads as 0xFF.
 * filename: the flash image file, or NULL or "" for FLASHFILENAME.
 * mem: receives a pointer to the statically allocated flash array.
 * Returns 0 if okay, or an explicit negative error code on failure.
 */
int flash_init(char *filename, uint32_t** mem);

/**
 * Copies FLASH_PAGE_CELLS elements from source buffer 'm' into flash page 'page'.
 * Updates both the in-memory array and persists the change to the simulation file.
 * Returns 0 if okay, ERR_FLASH_INVALID_SECTOR if page is not 0 to RAM_PAGE-1,
 * or another explicit negative error code on failure.
 */
int flash_program(uint32_t *m, int page);

/**
 * Simulation stub for filling in a random key at the end of the Root of Trust (RoT) sector.
 * Does nothing for the desktop implementation.
 * Returns 0.
 */
int flash_rndkey(void);

#endif /* FLASH_H */
