#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

/* ==========================================================================
   Block Storage Simulation Error Codes
   ========================================================================== */
#define ERR_BLK_CREATE_FAIL      (-13) /* Failed to create the binary simulation file */
#define ERR_BLK_WRITE_INIT       (-14) /* Failed to format initial blank file template */
#define ERR_BLK_OPEN_FAIL        (-15) /* Failed to open the mass storage file */
#define ERR_BLK_PARSE_FAIL       (-16) /* Block 0 header missing signature or parsing failed */
#define ERR_BLK_BOUNDS           (-17) /* Target block index exceeds allocated file capacity */
#define ERR_BLK_SEEK_FAIL        (-18) /* Failed to seek to target block offset */
#define ERR_BLK_READ_FAIL        (-19) /* Disk read failed to yield full 4KB payload */
#define ERR_BLK_WRITE_FAIL       (-20) /* Disk write failed to persist full 4KB payload */
#define ERR_BLK_WRITE_PROTECTED  (-21) /* Attempted write onto a write-protected block range */

/* Fixed block size as per specification: 0x1000 bytes = 4096 bytes = 1024 uint32_t cells */
#define BLK_SIZE_BYTES           4096
#define BLK_SIZE_CELLS           (BLK_SIZE_BYTES / sizeof(uint32_t))

/**
 * Initializes the block mass storage simulation.
 * Checks for file existence; if missing, formats a blank file filled with ASCII spaces
 * and writes the default Block 0 header template. Reads and caches write-protect sliders.
 * Returns 0 if okay, or a negative error code.
 */
int blk_init(char *filename);

/**
 * Reads a single 4KB block payload into the destination buffer.
 * Returns 0 if okay, or a negative error code.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a single 4KB block payload from the source buffer.
 * Enforces hardware write-protection limits parsed from Block 0.
 * Returns 0 if okay, or a negative error code (e.g., ERR_BLK_WRITE_PROTECTED).
 */
int blk_write(uint32_t blk, uint32_t *src);

#endif /* BLOCKS_H */
