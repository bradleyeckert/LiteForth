#ifndef API0_H
#define API0_H

#include <stdint.h>
#include "api0_list.h"

/**
 * @file api0.h
 * @brief Header file for the API 0 and API 1 dispatch routines.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * API 0 function indices, in the order of API0_LIST (see api0_list.h).
 * API0_COUNT is the number of API 0 functions in this build.
 */
enum api0_index {
#define API0_ENUM(id, fn) id,
    API0_LIST(API0_ENUM)
    API0_TOOLS_LIST(API0_ENUM)
#undef API0_ENUM
    API0_COUNT
};

/* The API0 instruction carries the index in a 9-bit field. */
typedef char api0_count_fits_9_bits[(API0_COUNT <= 512) ? 1 : -1];

/**
 * @brief Forth word `>options`  ( n -- )
 * Sets system option flags (SYS_OPTION_* in vm.h). A nonzero n ORs its
 * bits into the options; zero clears all options. Has no effect once
 * SYS_OPTIONS_LOCKED is set.
 *
 * @return 0.
 */
int lfAPI_setFlags(void);

/**
 * @brief Forth word `only`  ( -- )
 * Sets the search order to the minimal `only` wordlist.
 *
 * @return 0.
 */
int lfAPI_only(void);

/**
 * @brief Forth word `forth`  ( -- )
 * Replaces the first wordlist in the search order with the `forth` wordlist.
 *
 * @return 0.
 */
int lfAPI_forth(void);

/**
 * @brief Dispatches and executes an API 0 handler by index.
 * Called by the VM for the API 0 instruction; the index is the word's
 * position in API0_LIST (an api0_index value).
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