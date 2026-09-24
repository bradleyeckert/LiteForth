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
* String output functions
=========================================================================*/

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

// Generic string output
int serial_puts(const char* s) {
    int ior = 0;
    while (*s) {
        ior = serial_putc(*s++);
        if (ior) return ior;
    }
    return 0;
}

// Output a number
int lfDotB(uint32_t val, int base, int dpl, int digits) {
    char buf[36] = { 0 }; // Enough for 32-bit integer
    char* p = &buf[sizeof(buf)];
    *--p = 0; // Null terminator
    if ((val & 0x80000000) && (base == 10)) {
        val = 0 - val;
        serial_putc('-');
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
    int result = serial_puts(p);
    return result;
}

// Assume the terminal is okay with CRLF for newline
int lfCR(void) {
    return serial_puts("\r\n");
}

// Output a blank
int lfSpace(void) {
    return serial_putc(' ');
}

// Output a value
int lfDot(int32_t val) {
    int base = lfBASEfetch();
    lfDotB(val, base, 0, 0);
    if (base == 16) {
        serial_putc('H');
    }
    return lfSpace();
}

/*=========================================================================
* Address translation functions
=========================================================================*/

// Translate LiteForth address to system address.
int LFaddr_to_ptr(uint32_t addr, int32_t** ptr) {
    int bitfield_size = addr >> 27;
    int page = (addr >> (22 - VM_LOG2_PAGES)) & (VM_MEM_PAGES - 1);
    uint32_t a = addr & VM_PAGE_MASK;
    if (a >= vm_memory_rd_limit[page]) {
        return ERR_INVALID_ADDRESS;
    }
    **ptr = &vm_memory[page][a];
    return 0;
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

