#include <stdint.h>
#include <stdbool.h>
#include "options.h"
#include "errcodes.h"
#include "blocks.h"
#include "vm.h"
#include "forth.h"
#include "lfblocks.h"

static BlockBufferState buf_state[SYSTEM_BLOCKS];
static uint32_t lru_clock = 0;
static int active_buf_idx = -1; /* Index of the most recently accessed buffer */
static bool initialized = false;

/* Helper to initialize buffer tracking state */
static void init_buffer_system(void) {
    for (int i = 0; i < SYSTEM_BLOCKS; i++) {
        buf_state[i].block_id = UNASSIGNED_BLOCK;
        buf_state[i].is_dirty = false;
        buf_state[i].last_used = 0;
    }
    lru_clock = 0;
    active_buf_idx = -1;
    initialized = true;
}

/* Helper to get a direct C pointer to a block buffer's 4KB payload in RAM */
static uint32_t* get_buf_ptr(int buf_idx) {
    int32_t* ram_base = vm_memory[RAM_PAGE];
    int32_t* buf_base = ram_base + F_BLOCKBUFS;
    return (uint32_t*)(buf_base + (buf_idx * BLOCK_SIZE_CELLS));
}

/* Helper to write back a dirty buffer to mass storage */
static int sync_buffer(int buf_idx) {
    if (buf_state[buf_idx].block_id != UNASSIGNED_BLOCK && buf_state[buf_idx].is_dirty) {
        int err = blk_write(buf_state[buf_idx].block_id, get_buf_ptr(buf_idx));
        if (err != 0) {
            return err;
        }
        buf_state[buf_idx].is_dirty = false;
    }
    return 0;
}

/* Helper to find or allocate a buffer index using LRU eviction */
static int find_or_allocate_buffer(uint32_t blk, int* out_idx) {
    if (!initialized) {
        init_buffer_system();
    }

    // 1. Check if block is already present in a buffer
    for (int i = 0; i < SYSTEM_BLOCKS; i++) {
        if (buf_state[i].block_id == blk) {
            *out_idx = i;
            return 0;
        }
    }

    // 2. Search for an unassigned empty buffer
    for (int i = 0; i < SYSTEM_BLOCKS; i++) {
        if (buf_state[i].block_id == UNASSIGNED_BLOCK) {
            *out_idx = i;
            return 0;
        }
    }

    // 3. Evict the Least Recently Used (LRU) buffer
    int lru_idx = 0;
    uint32_t min_clock = buf_state[0].last_used;
    for (int i = 1; i < SYSTEM_BLOCKS; i++) {
        if (buf_state[i].last_used < min_clock) {
            min_clock = buf_state[i].last_used;
            lru_idx = i;
        }
    }

    // Flush dirty contents of evicted buffer to disk before reuse
    int err = sync_buffer(lru_idx);
    if (err != 0) {
        return err;
    }

    buf_state[lru_idx].block_id = UNASSIGNED_BLOCK;
    buf_state[lru_idx].is_dirty = false;
    *out_idx = lru_idx;
    return 0;
}

int lfAssignBlock(uint32_t blk, int32_t* f_addr) {

    // Check if block is already loaded in RAM
    if (initialized) {
        for (int i = 0; i < SYSTEM_BLOCKS; i++) {
            if (buf_state[i].block_id == blk) {
                buf_state[i].last_used = ++lru_clock;
                active_buf_idx = i;
                *f_addr = (int32_t)(LF_BLOCKBUFS + (i * BLOCK_SIZE_CELLS));
                return 0;
            }
        }
    }

    // Allocate buffer slot
    int buf_idx = -1;
    int err = find_or_allocate_buffer(blk, &buf_idx);
    if (err != 0) {
        return ERR_BLOCK_READ_ERROR;
    }

    // Read block data from disk into target RAM buffer
    err = blk_read(blk, get_buf_ptr(buf_idx));
    if (err != 0) {
        return err;
    }

    buf_state[buf_idx].block_id = blk;
    buf_state[buf_idx].is_dirty = false;
    buf_state[buf_idx].last_used = ++lru_clock;
    active_buf_idx = buf_idx;

    *f_addr = (int32_t)(LF_BLOCKBUFS + (buf_idx * BLOCK_SIZE_CELLS));
    return 0;
}


/**
 * BUFFER ( u -- addr )
 * Assigns a RAM buffer to block u without reading its data from mass storage.
 */
int lfAPI_buffer(void) {
    uint32_t blk = (uint32_t)vmPop();
    int buf_idx = -1;

    int err = find_or_allocate_buffer(blk, &buf_idx);
    if (err != 0) {
        return ERR_BLOCK_WRITE_ERROR;
    }

    buf_state[buf_idx].block_id = blk;
    buf_state[buf_idx].last_used = ++lru_clock;
    active_buf_idx = buf_idx;

    int32_t forth_addr = (int32_t)(LF_BLOCKBUFS + (buf_idx * BLOCK_SIZE_CELLS));
    vmPush(forth_addr);
    return 0;
}

/**
 * UPDATE ( -- )
 * Marks the currently active block buffer as dirty (modified).
 */
int lfAPI_update(void) {
    if (active_buf_idx >= 0 && active_buf_idx < SYSTEM_BLOCKS) {
        if (buf_state[active_buf_idx].block_id != UNASSIGNED_BLOCK) {
            buf_state[active_buf_idx].is_dirty = true;
        }
    }
    return 0;
}

/**
 * SAVE-BUFFERS ( -- )
 * Writes all modified (dirty) block buffers out to storage.
 */
int lfAPI_saveBuffers(void) {
    if (!initialized) {
        return 0;
    }

    for (int i = 0; i < SYSTEM_BLOCKS; i++) {
        int err = sync_buffer(i);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}

/**
 * FLUSH ( -- )
 * Saves all modified block buffers and unassigns all buffers.
 */
int lfAPI_flush(void) {
    int status = lfAPI_saveBuffers();
    if (status != 0) {
        return status;
    }
    return lfAPI_emptyBuffers();
}

/**
 * EMPTY-BUFFERS ( -- )
 * Unassigns all block buffers without saving modified contents (discards changes).
 */
int lfAPI_emptyBuffers(void) {
    init_buffer_system();
    return 0;
}

#define VM_PAGE_MASK    ((1 << (22 - VM_LOG2_PAGES)) - 1)

