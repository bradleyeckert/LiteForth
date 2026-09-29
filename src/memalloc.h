#ifndef MEMALLOC_H
#define MEMALLOC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Allocates an array of uint32_t elements from the stack pool.
 * Each allocation uses one extra element to record its size.
 * @param size The number of uint32_t elements to allocate (must be > 0).
 * @return Pointer to the allocated memory, or NULL if out of memory/invalid.
 */
void* pool_alloc(int size);

/**
 * @brief Frees the most recent allocation. Must be called in strict reverse order.
 * @param ptr The pointer returned by the matching pool_alloc call. NULL is ignored.
 * @return 0 if successfully freed (or ptr is NULL), or ERR_FREE_FAILED if the
 *         pool is empty or ptr is not the most recent allocation.
 */
int pool_free(void* ptr);

/**
 * @brief Instantly resets the entire stack pool, discarding all current allocations.
 */
void pool_reset(void);

/**
 * @brief Gets the remaining available capacity in the pool.
 * @return The number of unused uint32_t elements. The largest possible
 *         allocation is one less, because of the size record.
 */
int pool_unused(void);

#endif // MEMALLOC_H
