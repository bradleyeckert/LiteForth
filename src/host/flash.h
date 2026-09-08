#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include "options.h"

/* ==========================================================================
   Flash Simulation Error Codes
   ========================================================================== */
#define ERR_FLASH_CREATE_FAIL    (-7)  /* Failed to create the binary simulation file */
#define ERR_FLASH_WRITE_INIT     (-8)  /* Failed to write the full initial blank state to disk */
#define ERR_FLASH_INVALID_SECTOR (-9)  /* The requested sector index falls outside valid bounds */
#define ERR_FLASH_OPEN_WRITE     (-10) /* Failed to open the simulation file for writing/updating */
#define ERR_FLASH_SEEK_FAIL      (-11) /* Failed to seek to the start offset of the requested sector */
#define ERR_FLASH_WRITE_SECTOR   (-12) /* Failed to write the complete block data to the sector slot */

/* Global flash memory array, sized by configuration macros */
extern uint32_t flashmem[FLASHAPPSECTORSIZE * FLASHAPPSECTORS];

/**
 * Initializes the flash simulation for reading.
 * Checks for file existence; if missing, creates a blank file filled with 0xFF.
 * Loads the file contents into flashmem.
 * Returns 0 if okay, or an explicit negative error code on failure.
 */
int flash_init(char *filename);

/**
 * Copies FLASHAPPSECTORSIZE elements from source buffer 'm' into a specified Flash sector.
 * Updates both the in-memory array and persists the change to the simulation file.
 * Returns 0 if okay, or an explicit negative error code on failure.
 */
int flash_sector(uint32_t *m, int sector);

/**
 * Simulation stub for filling in a random key at the end of the Root of Trust (RoT) sector.
 * Does nothing for the desktop implementation.
 * Returns 0.
 */
int flash_rndkey(void);

#endif /* FLASH_H */
