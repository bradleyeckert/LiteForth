#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    int code;
    const char* msg;
} ErrorMapping;

/**
 * Returns the description of an error code, for error messages.
 * @param err_code Error code from errcodes.h.
 * @return "No error" for 0, the code's description, or "Unknown error code".
 */
const char* get_error_message(int err_code);

/**
 * Copies a null-terminated string, including the terminator, into text space
 * as 8-bit slices starting at the text pointer (TP), and advances TP past it.
 * TP is left as a byte address, as `_,"` does.
 * @param str String to copy.
 * @return 0 on success, or a negative error code (e.g., ERR_DICTIONARY_OVERFLOW).
 */
int lfCompString(char* str);

/**
 * @brief Forth word `t{`  ( -- )
 * Starts a test in the style of the Hayes test suite: T{ inputs -> results }T.
 * Records the stack depth.
 *
 * @return 0.
 */
int lfAPI_beginTest(void);

/**
 * @brief Forth word `->`  ( i*x -- )
 * Saves the stack contents as the actual results of the test and restores
 * the depth recorded by `t{`.
 *
 * @return 0, or ERR_STACK_OVERFLOW if the stack is too deep to save.
 */
int lfAPI_doTest(void);

/**
 * @brief Forth word `}t`  ( j*x -- )
 * Compares the stack with the results saved by `->`. If they match, restores
 * the depth recorded by `t{`.
 *
 * @return 0 if they match, ERR_WRONG_NUM_RESULTS if the depths differ,
 *         ERR_WRONG_RESULTS if any value differs, or ERR_STACK_OVERFLOW.
 */
int lfAPI_endTest(void);

/**
 * @brief Forth word `decimal`  ( -- )
 * Sets BASE to 10.
 *
 * @return 0.
 */
int lfAPI_decimal(void);

/**
 * @brief Forth word `hex`  ( -- )
 * Sets BASE to 16.
 *
 * @return 0.
 */
int lfAPI_hex(void);

/**
 * @brief Forth word `.page`  ( page -- )
 * Prints the memory map entry of one VM memory page: its base address,
 * write-protect, read and executable limits, and name.
 *
 * @return 0.
 */
int lfAPI_dotPage(void);

/**
 * @brief Forth word `.pages`  ( -- )
 * Prints the memory map entries of all VM memory pages, one per line.
 *
 * @return 0.
 */
int lfAPI_dotPages(void);

/**
 * @brief Forth word `dump`  ( addr len -- )
 * Displays len cells of VM memory starting at cell address addr, four cells
 * per line in hexadecimal followed by their ASCII bytes. Lines in the
 * write-protected part of a page are marked "read-only".
 *
 * @return 0 on success, or non-zero ior error code from vmFetch.
 */
int lfAPI_dump(void);

/**
 * @brief Forth word `dumpi`  ( inst -- )
 * Disassembles a single 16-bit instruction.
 *
 * @return 0.
 */
int lfAPI_dumpIns(void);

/**
 * @brief Forth word `dasm`  ( addr len -- )
 * Disassembles len instructions starting at instruction address addr (twice
 * the cell address). Lines that start a known word are labeled with its name.
 *
 * @return 0 on success, or non-zero ior error code from vmFetch.
 */
int lfAPI_dasm(void);

/**
 * @brief Forth word `.s`  ( -- )
 * Prints the data stack without changing it (see lfDotS).
 *
 * @return 0.
 */
int lfAPI_dotEss(void);

/**
 * @brief Forth word `.`  ( n -- )
 * Prints n in the current base, followed by a space (see lfDot).
 *
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfAPI_dot(void);

/**
 * @brief Forth word `see`  ( <name> -- )
 * Disassembles the definition of <name>. The length is estimated from the
 * next definition in the dictionary, up to 64 instructions.
 *
 * @return 0 on success, ERR_UNDEFINED_WORD if <name> is not found, or an
 *         error from vmFetch.
 */
int lfAPI_see(void);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */