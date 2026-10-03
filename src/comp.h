#ifndef COMP_H
#define COMP_H

#include <stdint.h>
#include "forth.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Compiling and Execution
 * ========================================================================*/

/**
 * Compiles a 32-bit integer literal into code space.
 * Uses `lit` with as many `pfx` prefixes as the value needs. A negative
 * value is compiled as its one's complement followed by `inv`.
 * @param x Signed literal integer value to encode.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCompileLit(int32_t x);

/**
 * Executes a dictionary word entry.
 * A constant pushes its value. A primitive runs as a single instruction.
 * Anything else is called at its code address and runs until it returns.
 * @param word Pointer to the target header structure.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfExecuteWord(const struct s_head* word);

/**
 * Executes an execution token, as stored in a header's `w` field.
 * If W_PRIMITIVE is set, the low 16 bits are one instruction (a micro-op
 * group, a macro, or a wide instruction such as an API or sys call) and run
 * once. Otherwise the low 23 bits are a code address, which is called and
 * runs until it returns. Unlike lfExecuteWord there is no header, so a
 * constant can't be recognized: its value would be treated as code.
 * Re-entrant, so it may be called from an API function (see `execute`).
 * @param xt Execution token.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfExecuteXT(uint32_t xt);

/**
 * Compiles a dictionary word entry.
 * A constant compiles as a literal, a primitive compiles inline as micro-ops
 * (or a whole instruction), and anything else compiles as a call.
 * @param word Pointer to the target header structure.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCompileWord(const struct s_head* word);

// globals
extern uint32_t lfCreatedName; // CREATE (comp.c) --> WORDLIST: VM address of the name, or 0

/**
 * Aligns the code pointer to an even instruction address (a cell boundary).
 * Flushes any partly built micro-op group, then pads with one zero
 * instruction if the code pointer is at an odd address.
 */
void lfCalign(void);

/**
 * @brief Forth word `,inst`  ( inst -- )
 * Compiles a raw 16-bit instruction, after flushing any pending micro-ops.
 *
 * @return 0 on success, or a negative error code (e.g., ERR_DICTIONARY_OVERFLOW).
 */
int lfAPI_inst(void);

/**
 * @brief Forth word `:`  ( <name> -- )
 * Aligns code space, creates a hidden (smudged) header for <name> at the
 * current code address, and switches STATE to compiling.
 * If <name> is an unresolved `label` in the search order, no header is made:
 * a jump to the current code address is stored in the label's reserved slots
 * and its A_UNRESOLVED flag is cleared.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_colon(void);

/**
 * @brief Forth word `:noname`  ( -- xt )
 * Starts an anonymous colon definition: aligns code space, pushes the
 * execution token of the code that follows, and switches STATE to
 * compiling. No header is made; `;` ends the definition.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_noname(void);

/**
 * LABEL  ( <name> -- )
 * Declares a word for a forward reference. Makes a header with A_UNRESOLVED
 * and reserves two code slots, but stays interpreting. Code can call the label
 * before it's defined. The slots hold an invalid opcode, so running such a
 * call before the label is resolved fails with ERR_INVALID_OPCODE. A later `: name` doesn't make a new header: it stores
 * a jump to its code in the reserved slots and clears A_UNRESOLVED.
 * @return 0 on success, or an ior from lfHeader.
 */
int lfAPI_label(void);

/**
 * @brief Forth word `;`  ( -- )  immediate
 * Reveals the word being defined (unless it was started by `:noname`),
 * returns STATE to interpreting, and
 * compiles its exit (turning a final call into a jump where possible).
 *
 * @return 0 on success, or ERR_UNSUPPORTED_OPERATION if no header exists yet.
 */
int lfAPI_semicolon(void);

/**
 * @brief Forth word `exit`  ( -- )  immediate
 * Compiles a return from the current definition. A call immediately before
 * it is converted to a jump (tail call) unless the call was marked
 * W_NO_TAIL_CALL.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_exit(void);

/**
 * @brief Forth word `constant`  ( n <name> -- )
 * Creates a header for <name> that pushes n when executed and compiles
 * n as a literal when compiled.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_constant(void);

/**
 * @brief Forth word `bits`  ( n <name> -- )
 * Allocates an n-bit slice at HERE in the current memory space and creates
 * a constant <name> holding its slice address. HERE advances past the slice.
 *
 * @return 0 on success, ERR_TOO_MANY_BITS if n is not 1 to 32, or another
 *         negative error code.
 */
int lfAPI_bits(void);

/**
 * @brief Forth word `create`  ( <name> -- )
 * Creates a word that pushes the current HERE of the active memory space.
 * Its code is a literal followed by an exit, plus one reserved instruction
 * that `does>` can patch.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_dotCreate(void);

/**
 * @brief Forth word `does>`  ( -- )  immediate
 * Patches the most recent `create`d word so that, after pushing its
 * address, it jumps to the code that follows `does>`.
 *
 * @return 0 on success, or ERR_UNSUPPORTED_OPERATION if there is no
 *         pending `create`.
 */
int lfAPI_dotDoes(void);

/**
 * @brief Forth word `>body`  ( xt -- addr )
 * Returns the data address of a word made by `create`, by decoding the
 * `pfx`/`lit` instructions at the start of its code.
 *
 * @return 0 on success, or ERR_BODY_ON_NON_CREATE if xt was not made by `create`.
 */
int lfAPI_toBody(void);

/**
 * @brief Forth word `bit`  ( n -- )
 * Sets the slice width, in bits, of HERE in the current memory space, so
 * subsequent allocations use n-bit slices (32 means whole cells). Starts a
 * new cell if an n-bit slice would not fit in the current one.
 *
 * @return 0 on success, or ERR_TOO_MANY_BITS if n is not 1 to 32.
 */
int lfAPI_bit(void);

/**
 * @brief Forth word `postpone`  ( <name> -- )  immediate
 * Compiles an immediate word directly. For any other word, compiles code
 * that will compile it (via `,compile`) when the current definition runs.
 *
 * @return 0 on success, ERR_UNDEFINED_WORD if <name> is not found,
 *         ERR_POSTPONING_CONSTANT for a constant, or another negative error code.
 */
int lfAPI_postpone(void);

/**
 * @brief Forth word `,compile`  ( xt -- )
 * Compiles an execution token: inline micro-ops for a primitive, or a call.
 * This is the run-time half of `postpone`.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_compile(void);

/**
 * @brief Forth word `literal`  ( n -- )  immediate
 * Compiles n as a literal.
 *
 * @return 0 on success, or a negative error code.
 */
int lfAPI_literal(void);

/**
 * @brief Forth word `break`  ( -- )
 * Ends the current micro-op group so the next primitive starts a new
 * instruction. Also cancels tail-call conversion of the preceding call.
 *
 * @return 0.
 */
int lfAPI_newinst(void);

#ifdef __cplusplus
}
#endif

#endif /* COMP_H */