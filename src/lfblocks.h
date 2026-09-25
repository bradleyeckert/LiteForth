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

/* External VM interface memory and stack handlers */
extern int32_t* vm_memory[VM_MEM_PAGES];
extern int32_t vmPop(void);
extern int vmPush(int32_t val);

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
 * @brief Forth stack effect: ( u -- addr )
 * Returns the RAM memory address of the buffer containing block `u`.
 * If the block is not currently in RAM, it reads it from storage.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_block(void);

/**
 * @brief Forth stack effect: ( u -- addr )
 * Assigns a RAM buffer to block `u` without reading its contents from disk.
 * Used when overwriting an entire block from scratch.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_buffer(void);

/**
 * @brief Forth stack effect: ( -- )
 * Marks the currently active block buffer as "dirty" (modified).
 * The system will save dirty buffers back to storage upon reuse or flush.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_update(void);

/**
 * @brief Forth stack effect: ( -- )
 * Writes all modified (dirty) block buffers out to storage.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_saveBuffers(void);

/**
 * @brief Forth stack effect: ( -- )
 * Executes `lfAPI_saveBuffers` and marks all block buffers as unassigned/empty.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_flush(void);

/**
 * @brief Forth stack effect: ( -- )
 * Unassigns all block buffers without saving modified contents to storage.
 * Discards all unsaved changes in memory.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_emptyBuffers(void);

int lfAPI_load(void);

#ifdef __cplusplus
}
#endif

#endif /* LFBLOCKS_H */