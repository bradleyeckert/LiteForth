/*
 * The VM's I/O space (iomap.h): the terminal, the LCD and the math unit, reached
 * by `>sys` and `sys>` with an index of IOMAP_FIRST or more.
 */
#include <stdint.h>
#include "iomap.h"
#include "errcodes.h"
#include "serial_io.h"
#include "tools.h"                      /* lf_putc, divide64by32 */
#include "lcd.h"                        /* lcd_tx: the target's */

/* ---- Math unit --------------------------------------------------------- */

static uint32_t a[2];                   /* A0, A1: written at IO_MATH_ARG + n */
static uint32_t y[3];                   /* Y0, Y1, Y2: read at IO_MATH_RES + n */
static int busy;                        /* status reads left that say busy */
int iomap_math_latency;

/* An operation's results: Y0, Y1, Y2 */
static void result(uint32_t y0, uint32_t y1, uint32_t y2) {
    y[0] = y0;  y[1] = y1;  y[2] = y2;
    busy = iomap_math_latency;
}

/* um* and m*: A0 * tos, Y1:Y0 */
static void multiply(uint32_t tos, int sign) {
    uint32_t m = a[0], n = tos;
    int invert = 0;
    if (sign) {
        invert = ((m ^ n) & 0x80000000u) != 0;
        if (m & 0x80000000u) m = 0 - m;
        if (n & 0x80000000u) n = 0 - n;
    }
    uint64_t p = (uint64_t)m * (uint64_t)n;
    if (invert) {
        p = 0 - p;
    }
    result((uint32_t)p, (uint32_t)(p >> 32), y[2]);
}

/* mu/mod: A0:tos / A1, unsigned: quotient Y1:Y0, remainder Y2. A divisor
   of 0 starts nothing. */
static int mudivmod(uint32_t tos) {
    uint32_t divisor = a[1];
    if (divisor == 0) {
        return ERR_DIVISION_BY_ZERO;
    }
    uint64_t dividend = ((uint64_t)a[0] << 32) | tos;
    uint32_t rem = 0;
    uint64_t q = divide64by32(dividend, divisor, &rem);
    result((uint32_t)q, (uint32_t)(q >> 32), rem);
    return 0;
}

/* star-slash-mod: A0 * tos / A1, signed and symmetric (the remainder takes
   the dividend's sign): quotient Y0, remainder Y2. A divisor of 0 starts
   nothing. */
static int stardivmod(uint32_t tos) {
    int32_t divisor = (int32_t)a[1];
    if (divisor == 0) {
        return ERR_DIVISION_BY_ZERO;
    }
    int64_t d = (int64_t)(int32_t)a[0] * (int64_t)(int32_t)tos;
    int dividend_negative = d < 0;
    int quot_negative = dividend_negative ^ (divisor < 0);
    uint64_t u_dividend = dividend_negative ? (uint64_t)0 - (uint64_t)d : (uint64_t)d;
    uint32_t u_divisor = divisor < 0 ? 0 - (uint32_t)divisor : (uint32_t)divisor;
    uint32_t u_rem = 0;
    uint64_t u_quot = divide64by32(u_dividend, u_divisor, &u_rem);
    uint32_t quot = quot_negative ? 0 - (uint32_t)u_quot : (uint32_t)u_quot;
    uint32_t rem = dividend_negative ? 0 - u_rem : u_rem;
    result(quot, y[1], rem);
    return 0;
}

/* ---- The ports --------------------------------------------------------- */

int iomap_rd(int addr, uint32_t *tos) {
    *tos = 0;
    switch (addr) {
    case IO_TERM_DATA:
        *tos = (uint32_t)serial_getc();
        return 0;
    case IO_TERM_RXQ:
        *tos = (uint32_t)serial_ready();
        return 0;
    case IO_TERM_TXQ:
        *tos = (uint32_t)serial_busy();
        return 0;
    case IO_MATH_STATUS:
        if (busy) {
            busy--;
            *tos = 1;
        }
        return 0;
    case IO_MATH_RES:
    case IO_MATH_RES + 1:
    case IO_MATH_RES + 2:
        if (busy) {
            return ERR_UNSUPPORTED_OPERATION;   /* poll IO_MATH_STATUS first */
        }
        *tos = y[addr - IO_MATH_RES];
        return 0;
    default:
        return ERR_INVALID_ADDRESS;
    }
}

int iomap_wr(int addr, uint32_t tos) {
    switch (addr) {
    case IO_TERM_DATA:
        return lf_putc((char)tos);
    case IO_LCD_TX:
        return lcd_tx((uint16_t)tos);
    case IO_MATH_ARG:
    case IO_MATH_ARG + 1:
        a[addr - IO_MATH_ARG] = tos;
        return 0;
    case IO_MATH_UMSTAR:
    case IO_MATH_MSTAR:
        multiply(tos, addr == IO_MATH_MSTAR);
        return 0;
    case IO_MATH_MUDIVMOD:
        return mudivmod(tos);
    case IO_MATH_STARDIVMOD:
        return stardivmod(tos);
    default:
        return ERR_INVALID_ADDRESS;
    }
}
