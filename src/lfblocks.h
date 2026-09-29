#ifndef LFBLOCKS_H
#define LFBLOCKS_H

#include <stdint.h>
#include <stdbool.h>
#include "options.h"
#include "errcodes.h"
#include "blocks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sentinel value used for unassigned block buffer slots */
#define UNASSIGNED_BLOCK 0xFFFFFFFFU

/* -------------------------------------------------------------------------
 * Data Types
 * ------------------------------------------------------------------------- */

/**
 * Tracking structure for each system block RAM buffer.
 */
typedef struct {
    uint32_t block_id;   /* Assigned block number (or UNASSIGNED_BLOCK) */
    bool is_dirty;       /* True if buffer contents were updated */
    uint32_t last_used;  /* LRU timestamp counter */
} BlockBufferState;

/* -------------------------------------------------------------------------
 * LiteForth Block Management API (Forth Words Interface)
 * ------------------------------------------------------------------------- */

/**
 * @brief Forth word `block`  ( u -- addr )
 * Returns the address of a RAM buffer holding block u, reading the block
 * from storage if it is not already in a buffer.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_block(void);

/**
 * @brief Forth word `buffer`  ( u -- addr )
 * Assigns a RAM buffer to block `u` without reading its contents from storage.
 * Used when overwriting an entire block from scratch. If every buffer is in
 * use, the least recently used one is reused, saving it first if modified.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_buffer(void);

/**
 * @brief Forth word `update`  ( -- )
 * Marks the most recently accessed block buffer as "dirty" (modified).
 * The system will save dirty buffers back to storage upon reuse or flush.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_update(void);

/**
 * @brief Forth word `save-buffers`  ( -- )
 * Writes all modified (dirty) block buffers out to storage.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_saveBuffers(void);

/**
 * @brief Forth word `flush`  ( -- )
 * Saves all modified buffers (like `save-buffers`), then unassigns all block buffers.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_flush(void);

/**
 * @brief Forth word `empty-buffers`  ( -- )
 * Unassigns all block buffers without saving modified contents to storage.
 * Discards all unsaved changes in memory.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_emptyBuffers(void);

/**
 * Finds or assigns a RAM buffer for a block, reading the block from storage
 * if it is not already in a buffer. The buffer becomes the one `update` marks.
 * Shared by `block` and `load`.
 *
 * @param blk Block number.
 * @param f_addr Receives the Forth address of the buffer in the RAM page.
 * @return 0 on success, ERR_BLOCK_READ_ERROR if a modified buffer could not be
 *         saved to make room, or the error from blk_read.
 */
int lfAssignBlock(uint32_t blk, int32_t* f_addr);

#ifdef __cplusplus
}
#endif

#endif /* LFBLOCKS_H */