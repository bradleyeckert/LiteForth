#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include "utils.h"
#include "flash.h"
#include "comp.h"
#include "tools.h"

/*=========================================================================
* Math
=========================================================================*/

// Align the address to fit the slice and set its new slice size
uint32_t lfSetSliceWidth(uint32_t addr, int bits) {
    int position = (addr >> 22) & 0x1F;
    // A 32-bit slice is a whole cell, encoded as size 0 and position 0.
    // Widths outside 1..31 are treated the same way (and avoid dividing by 0).
    if ((bits < 1) || (bits > 31)) {
        if (position) addr = (addr & 0x3FFFFF) + 1;  // start a new cell
        return addr & 0x3FFFFF;
    }
    // align to next slice
    position = ((position + bits - 1) / bits) * bits;
    // align to cell if crossing cell boundaries
    if ((position + bits) > 32) {
        addr = (addr & ~(0x1F << 22)) + 1;
    }
    // insert the new size field
    return (addr & ~(0x1Fu << 27)) | ((uint32_t)bits << 27);
}

// A basic division primitive for numeric conversion
uint64_t divide64by32(uint64_t dividend, uint32_t divisor,
    uint32_t* remainder) {

    uint64_t q = 0;
    uint64_t rem = 0;

    for (int i = 63; i >= 0; i--) {
        rem <<= 1;
        rem |= (dividend >> i) & 1ULL;

        if (rem >= (uint64_t)divisor) {
            rem -= (uint64_t)divisor;
            q |= (1ULL << i);
        }
    }
    if (remainder != NULL) *remainder = (uint32_t)rem;
    return q;
}

/*=========================================================================
* String output functions
=========================================================================*/

int lf_putc(char c) {
    return serial_putc(c);
}

// Generic string output
int lf_puts(const char* s) {
    int ior = 0;
    while (*s) {
        ior = lf_putc(*s++);
        if (ior) return ior;
    }
    return 0;
}

// Sets text color for the terminal
int lfSetColor(int color) {
    int ior = 0;
    if (g_lf_sys_options & SYS_OPTION_USE_COLORS) {
        lf_puts("\033[");
        // Standard Colors
        if (color >= 0 && color <= 7) {
            lf_putc('3');
            lf_putc('0' + color);
        } // Bright / High-Intensity Colors
        else if (color >= 8 && color <= 15) {
            lf_putc('9');
            lf_putc('0' + (color - 8));
        } // Reset / Normal
        else {
            lf_putc('0');
        }
        ior = lf_putc('m');
    }
    return ior;
}

// Output a number
int lfDotB(uint32_t val, int base, int dpl, int digits) {
    char buf[36] = { 0 }; // Enough for 32-bit integer
    char* p = &buf[sizeof(buf)];
    *--p = 0; // Null terminator
    if ((val & 0x80000000) && (base == 10)) {
        val = 0 - val;
        lf_putc('-');
    }
    do {
        if (p == buf) break; // no more room
        int digit = val % base;
        val /= base;
        if (digit < 10) {
            *--p = '0' + digit;
        }
        else {
            *--p = 'A' + (digit - 10);
        }
        digits--;
        if (dpl) {          // optional decimal
            if (--dpl == 0) {
                *--p = '.';
            }
        }
    } while (val | dpl | (digits > 0));
    int result = lf_puts(p);
    return result;
}

// Assume the terminal is okay with CRLF for newline
int lfCR(void) {
    return lf_puts("\r\n");
}

// Output a blank
int lfSpace(void) {
    return lf_putc(' ');
}

// Output a value
int lfDot(int32_t val) {
    int base = lfBASEfetch();
    int size = (val >> 27) & 0x1F;
    int pos = (val >> 22) & 0x1F;
    // In hex, show slice addresses as size:pos:addr. A real slice always fits
    // in its cell (pos + size <= 32). Other bases always show plain numbers,
    // since e.g. a 16-bit slice address looks the same as a negative number.
    if ((base == 16) && (size > 0) && (size <= 16) && ((pos + size) <= 32)) {
        lfDotB(size, base, 0, 0);
        lf_putc(':');
        lfDotB(pos, base, 0, 0);
        lf_putc(':');
        val &= 0x3FFFFF;
    }
    lfDotB(val, base, 0, 0);
    if (base == 16) {
        lf_putc('H');
    }
    return lfSpace();
}

// Output a value, decimal only, no trailing space
int lfDot10(int32_t val) {
    return lfDotB(val, 10, 0, 0);
}

/**
 * Attempts to interpret a raw token text as a numeric literal.
 * Returns ior and sets `val` to the parsed number.
 * It sets DPL to the number of digits after the decimal point if a decimal
 * point is present, leaves it at -1 otherwise.
 */

static int char2digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1; // Invalid character for a digit
}

int lfParseNumber(char* token, int base, int32_t* val) {
    int dpl = -1; // -1 indicates no decimal point was encountered
    int32_t value = 0;
    int i = 0;
    int sign = 0;
    int has_digits = 0;
    int has_dpl = 0;

    // Fast-fail empty tokens
    if (token[0] == '\0') return ERR_UNDEFINED_WORD; // -13

    while (1) {
        char c = token[i++];
        if (c == '\0') break;

        // Handle leading minus sign
        if (i == 1 && c == '-') {
            sign = 1;
            continue;
        }

        // Handle decimal point '.'
        if (c == '.') {
            has_dpl = 1;
            dpl = 0;
            continue;
        }

        int digit = char2digit(c);

        // Guard against invalid characters or digits >= base
        if (digit < 0 || digit >= base) {
            return ERR_UNDEFINED_WORD;
        }

        has_digits = 1;
        value = (value * base) + digit;

        // Increment DPL for every valid digit parsed after '.'
        if (has_dpl) {
            dpl++;
        }
    }

    // Must have contained at least one actual digit
    if (!has_digits) {
        return ERR_UNDEFINED_WORD;
    }

    if (sign) {
        value = -value;
    }

    *val = value;
    return vmStore(LF_DPL, dpl);
}


/*=========================================================================
* System word fetch and store
=========================================================================*/

int vmPush(int32_t x) {
    return vmPoke(-1, x);
}

int32_t vmPop(void) {
    return vmPeek(-1);
}

int lfSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_STATE, &result);
    return result;
}

int lfSTATEstore(int state) {
    return vmStore(LF_STATE, state);
}

int lfBASEfetch(void) {
    int32_t result;
    vmFetch(LF_BASE, &result);
    return result;
}

int lfBASEstore(int base) {
    return vmStore(LF_BASE, base);
}

int lfTpFetch(int32_t* tp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    *tp = mem[F_PTRS_TP];
    return 0;
}

int lfTpStore(int32_t tp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    uint32_t tp_max = mem[F_PTRS_TP + 1];
    if (((unsigned)tp & 0x3FFFFF) >= tp_max) return ERR_DICTIONARY_OVERFLOW;
    mem[F_PTRS_TP] = tp;
    return 0;
}
