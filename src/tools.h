#ifndef TOOLS_H
#define TOOLS_H

#include <stdint.h>

/**
 * @file tools.h
 * @brief Header file for small standalone functions used by LiteForth
 */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * String output functions
 * ========================================================================*/

/**
 * Transmits a null-terminated string to the terminal (serial port or stdio).
 * Stops at the first character that fails to send.
 * @param s String to transmit.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lf_puts(const char* s);

/**
 * Formats and outputs a number in the specified base.
 * In base 10 the value is treated as signed; in other bases, as unsigned.
 * Digits above 9 are shown as uppercase letters.
 * @param val Number to output.
 * @param base Number base (e.g., 10 for decimal, 16 for hex).
 * @param dpl Number of digits to place after a decimal point, or 0 for none.
 * @param digits Minimum number of digits; the number is padded with leading zeros.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfDotB(uint32_t val, int base, int dpl, int digits);

/**
 * Outputs a carriage return and line feed sequence ("\r\n").
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCR(void);

/**
 * Outputs a single space character.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfSpace(void);

/**
 * Outputs a single character to the terminal (serial port or stdio).
 * @param c Character to send.
 * @return 0 on success, or ERR_TERM_TX_FAILED on failure.
 */
int lf_putc(char c);

/**
 * Formats and outputs a 32-bit value in the current BASE, appending 'H' in
 * hexadecimal and a trailing space. If bits 31:27 of the value are 1 to 16,
 * it is printed as a slice address in the form size:position:address.
 * @param val Value to print.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfDot(int32_t val);

/**
 * Outputs a signed number in decimal, with no trailing space.
 * @param val Number to print.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfDot10(int32_t val);

/**
 * Sets the terminal text color with an ANSI escape sequence.
 * Does nothing unless SYS_OPTION_USE_COLORS is set.
 * @param color COLOR_* value from forth.h: 0-7 standard, 8-15 bright,
 *        any other value (e.g., COLOR_NORMAL) resets to the default color.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfSetColor(int color);

/**
 * @brief Performs 64-bit dividend by 32-bit divisor unsigned division.
 *
 * @param dividend 64-bit unsigned dividend.
 * @param divisor  32-bit unsigned divisor. Must not be 0.
 * @param remainder Pointer to store the 32-bit remainder (can be NULL).
 * @return 64-bit unsigned quotient.
 */
uint64_t divide64by32(uint64_t dividend, uint32_t divisor, uint32_t* remainder);

/* =========================================================================
 * Data stack
 * ========================================================================*/

/**
 * Pushes a 32-bit value onto the VM data stack.
 * @param value Value to push.
 * @return 0.
 */
int vmPush(int32_t value);

/**
 * Pops a 32-bit value from the VM data stack.
 * There is no underflow check here; QUIT checks the stack depth after each line.
 * @return The popped value.
 */
int32_t vmPop(void);

/* =========================================================================
 * System variables
 * ========================================================================*/

/**
 * Reads the number conversion base (BASE).
 * @return The current base, e.g. 10 or 16.
 */
int lfBASEfetch(void);

/**
 * Sets the number conversion base (BASE).
 * @param base New base, 2 to 36. BASE is a 6-bit field, so larger values are truncated.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfBASEstore(int base);

/**
 * Sets the compiler state (STATE).
 * @param state 1 to compile, 0 to interpret.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfSTATEstore(int state);

/**
 * Reads the compiler state (STATE).
 * @return 1 while compiling, 0 while interpreting.
 */
int lfSTATEfetch(void);

/**
 * Reads the text pointer (TP), where the next header name is stored.
 * @param tp Receives the text pointer.
 * @return 0.
 */
int lfTpFetch(int32_t* tp);

/**
 * Sets the text pointer (TP).
 * @param tp New text pointer.
 * @return 0 on success, or ERR_DICTIONARY_OVERFLOW if tp is at or past the
 *         end of text space.
 */
int lfTpStore(int32_t tp);

/* =========================================================================
 * Numbers and slices
 * ========================================================================*/

/**
 * Converts a token to a number in the given base.
 * Accepts an optional leading '-' and one or more digits, with an optional
 * '.' anywhere. Sets DPL to the number of digits after the '.', or to -1 if
 * there is no '.'. Overflow is not detected.
 * @param token Null-terminated text to convert.
 * @param base Number base, 2 to 36; letters in either case are digits above 9.
 * @param val Receives the number on success.
 * @return 0 on success, or ERR_UNDEFINED_WORD if the token is not a number.
 */
int parseNumber(char* token, int base, int32_t* val);

/**
 * Sets the slice width of an address.
 * Moves the bit position up to the next multiple of bits, and to the start
 * of the next cell if a slice of that width would not fit in the current one.
 * @param addr Cell or slice address.
 * @param bits Slice width in bits, 1 to 31.
 * @return The address with the new width and aligned position.
 */
uint32_t lfSetSliceWidth(uint32_t addr, int bits);

#ifdef __cplusplus
}
#endif

#endif /* TOOLS_H */