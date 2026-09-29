#ifndef API0_H
#define API0_H

#include <stdint.h>

/**
 * @file api0.h
 * @brief Header file for the API 0 and API 1 dispatch routines.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dispatches and executes an API 0 handler by index.
 * Called by the VM for the API 0 instruction; the index is the word's
 * position in the API0fns table in api0.c.
 *
 * @param fn API 0 function table index.
 * @return 0 on success, or an error code (e.g., ERR_INVALID_API_CALL, ERR_QUIT).
 */
int VMapi0Call(int fn);

/**
 * @brief External API 1 function dispatcher stub.
 *
 * @param fn API 1 function table index.
 * @return ERR_INVALID_API_CALL.
 */
int VMapi1Call(int fn);


#ifdef __cplusplus
}
#endif

#endif /* API0_H */