/*
 * The VM's I/O space (iomap.h): the terminal and the math unit, reached
 * by `>sys` and `sys>` with an index of IOMAP_FIRST or more.
 */
#include <stdint.h>
#include "iomap.h"
#include "errcodes.h"
#include "serial_io.h"
#include "tools.h"                      /* lf_putc, divide64by32 */

/* ---- Math unit --------------------------------------------------------- */

static uint32_t arg[3];                 /* A0 (the last written), A1, A2 */
static uint32_t res[3];
static int res_count, res_next;

static uint32_t result(int n, uint32_t r0, uint32_t r1, uint32_t r2) {
    res[0] = r0;  res[1] = r1;  res[2] = r2;
    res_count = n;
    res_next = 1;
    return r0;
}

/* um* and m*: A0 * A1, low cell then high */
static uint32_t multiply(int sign) {
    uint32_t a = arg[0], b = arg[1];
    int invert = 0;
    if (sign) {
        invert = ((a ^ b) & 0x80000000u) != 0;
        if (a & 0x80000000u) a = 0 - a;
        if (b & 0x80000000u) b = 0 - b;
    }
    uint64_t p = (uint64_t)a * (uint64_t)b;
    if (invert) {
        p = 0 - p;
    }
    return result(2, (uint32_t)p, (uint32_t)(p >> 32), 0);
}

/* mu/mod: A1:A0 / A2, unsigned: remainder, quotient low, quotient high */
static int mudivmod(uint32_t *data) {
    uint32_t divisor = arg[2];
    if (divisor == 0) {
        return ERR_DIVISION_BY_ZERO;
    }
    uint64_t dividend = ((uint64_t)arg[1] << 32) | arg[0];
    uint32_t rem = 0;
    uint64_t q = divide64by32(dividend, divisor, &rem);
    *data = result(3, rem, (uint32_t)q, (uint32_t)(q >> 32));
    return 0;
}

/* star-slash-mod: A0 * A1 / A2, signed and symmetric (the remainder takes the
   dividend's sign): remainder, quotient */
static int stardivmod(uint32_t *data) {
    int32_t divisor = (int32_t)arg[2];
    if (divisor == 0) {
        return ERR_DIVISION_BY_ZERO;
    }
    int64_t d = (int64_t)(int32_t)arg[0] * (int64_t)(int32_t)arg[1];
    int dividend_negative = d < 0;
    int quot_negative = dividend_negative ^ (divisor < 0);
    uint64_t u_dividend = dividend_negative ? (uint64_t)0 - (uint64_t)d : (uint64_t)d;
    uint32_t u_divisor = divisor < 0 ? 0 - (uint32_t)divisor : (uint32_t)divisor;
    uint32_t u_rem = 0;
    uint64_t u_quot = divide64by32(u_dividend, u_divisor, &u_rem);
    uint32_t quot = quot_negative ? 0 - (uint32_t)u_quot : (uint32_t)u_quot;
    uint32_t rem = dividend_negative ? 0 - u_rem : u_rem;
    *data = result(2, rem, quot, 0);
    return 0;
}

/* ---- The ports --------------------------------------------------------- */

int iomap_rd(int addr, uint32_t *data) {
    *data = 0;
    switch (addr) {
    case IO_TERM_DATA:
        *data = (uint32_t)serial_getc();
        return 0;
    case IO_TERM_RXQ:
        *data = (uint32_t)serial_ready();
        return 0;
    case IO_TERM_TXQ:
        *data = (uint32_t)serial_busy();
        return 0;
    case IO_MATH_RES:
        if (res_next < res_count) {
            *data = res[res_next++];
        }
        return 0;
    case IO_MATH_UMSTAR:
        *data = multiply(0);
        return 0;
    case IO_MATH_MSTAR:
        *data = multiply(1);
        return 0;
    case IO_MATH_MUDIVMOD:
        return mudivmod(data);
    case IO_MATH_STARDIVMOD:
        return stardivmod(data);
    default:
        return ERR_INVALID_ADDRESS;
    }
}

int iomap_wr(int addr, uint32_t data) {
    switch (addr) {
    case IO_TERM_DATA:
        return lf_putc((char)data);
    case IO_MATH_ARG:
        arg[2] = arg[1];
        arg[1] = arg[0];
        arg[0] = data;
        return 0;
    default:
        return ERR_INVALID_ADDRESS;
    }
}
