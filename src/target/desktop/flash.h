#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/**
 * Initializes the flash simulation for reading.
 * Checks for file existence; if missing, creates a blank file filled with 0xFF.
 * Loads the file contents into flashmem.
 * *mem is a pointer to the statically allocated flash, to avoid global
 * Returns 0 if okay, or an explicit negative error code on failure.
 */
int flash_init(char *filename, int32_t** mem);

/**
 * Copies FLASHAPPSECTORSIZE elements from source buffer 'm' into a specified Flash sector.
 * Updates both the in-memory array and persists the change to the simulation file.
 * Returns 0 if okay, or an explicit negative error code on failure.
 */
int flash_program(uint32_t *m, int page);

/**
 * Simulation stub for filling in a random key at the end of the Root of Trust (RoT) sector.
 * Does nothing for the desktop implementation.
 * Returns 0.
 */
int flash_rndkey(void);

#endif /* FLASH_H */
