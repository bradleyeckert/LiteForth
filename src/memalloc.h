#ifndef MEMALLOC_H
#define MEMALLOC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Allocates an array of uint32_t elements from the stack pool.
 * @param size The number of uint32_t elements to allocate.
 * @return Pointer to the allocated memory, or NULL if out of memory/invalid.
 */
void* pool_alloc(int size);

/**
 * @brief Frees the most recent allocation. Must be called in strict reverse order.
 * @param ptr The pointer returned by the matching pool_alloc call.
 * @return 0 if successfully freed, -1 if the pointer violates strict LIFO order.
 */
int pool_free(void* ptr);

/**
 * @brief Instantly resets the entire stack pool, discarding all current allocations.
 */
void pool_reset(void);

/**
 * @brief Gets the remaining available capacity in the pool.
 * @return The number of unused uint32_t elements available for allocation.
 */
int pool_unused(void);

#endif // MEMALLOC_H
