#ifndef COMP_H
#define COMP_H

#include <stdint.h>
#include "forth.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Stack operations
 * ========================================================================*/

/* =========================================================================
 * Compiling and Execution
 * ========================================================================*/

/**
 * Compiles a 32-bit integer literal.
 * @param x Signed literal integer value to encode.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCompileLit(int32_t x);

/**
 * Executes a dictionary word entry.
 * @param word Pointer to the target header structure.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfExecuteWord(const struct s_head* word);

/**
 * Compiles a dictionary word entry.
 * @param word Pointer to the target header structure.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCompileWord(const struct s_head* word);

// globals
extern char* lfCreatedName; // CREATE (comp.c) --> WORDLIST (forth.c)

void lfCalign(void);

int lfAPI_inst(void);
int lfAPI_colon(void);
int lfAPI_semicolon(void);
int lfAPI_exit(void);
int lfAPI_constant(void);
int lfAPI_bits(void);
int lfAPI_dotCreate(void);
int lfAPI_dotDoes(void);
int lfAPI_toBody(void);
int lfAPI_bit(void);
int lfAPI_postpone(void);
int lfAPI_compile(void);
int lfAPI_literal(void);
int lfAPI_break(void);

#ifdef __cplusplus
}
#endif

#endif /* COMP_H */