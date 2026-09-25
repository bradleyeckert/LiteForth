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
  * Transmits a null-terminated string over the serial port.
  * @param s String to transmit.
  * @return 0 on success, or an explicit negative error code on failure.
  */
int serial_puts(const char* s);

/**
 * Formats and outputs a numeric value in the specified base over serial.
 * @param val Number to output.
 * @param base Number base (e.g., 10 for decimal, 16 for hex).
 * @param dpl Decimal point location/count.
 * @param digits Minimum digits to display.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfDotB(uint32_t val, int base, int dpl, int digits);

/**
 * Outputs a carriage return and line feed sequence ("\r\n").
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfCR(void);

/**
 * Outputs a single space character over serial.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfSpace(void);

/**
 * Formats and outputs a signed 32-bit integer according to BASE, appending
 * 'H' for hexadecimal and trailing with a space.
 * @param val Integer to print.
 * @return 0 on success, or an explicit negative error code on failure.
 */
int lfDot(int32_t val);
	
/**
 * @brief Performs 64-bit dividend by 32-bit divisor unsigned division.
 *
 * @param dividend 64-bit unsigned dividend.
 * @param divisor  32-bit unsigned divisor.
 * @param remainder Pointer to store the 32-bit remainder (can be NULL).
 * @return 64-bit unsigned quotient.
 */
uint64_t divide64by32(uint64_t dividend, uint32_t divisor, uint32_t* remainder);

//int LFaddr_to_ptr(uint32_t addr, int32_t** ptr);
int vmPush(int32_t value);
int32_t vmPop(void);
int lfBASEfetch(void);
int lfBASEstore(int base);
int lfSTATEstore(int state);
int lfSTATEfetch(void);

int parseNumber(char* token, int base, int32_t* val);


#ifdef __cplusplus
}
#endif

#endif /* TOOLS_H */