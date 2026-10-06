#ifndef IOPAGE_H
#define IOPAGE_H

#include <stdint.h>

/*
 * The CH32H417's peripheral registers as a VM memory page.
 *
 * With VM_IO_PAGE defined in options.h, main.c maps that page onto the
 * peripheral block at IO_BASE, IO_CELLS cells long, writable and not
 * executable. Cell c of the page is the register at IO_BASE + 4c, so its VM
 * address is (VM_IO_PAGE << 19) + c: GPIOA (0x40010800) is at 0x284200 in
 * page 5.
 *
 * The block has gaps with no peripheral behind them, and an access there may
 * fault the bus. vmFetch and vmStore call vmIoValid for every access to the
 * page, so a stray read (`dump`, a wrong address) fails with
 * ERR_INVALID_ADDRESS instead.
 *
 * Reads and writes are whole 32-bit words. Reading a register can have side
 * effects (a data register pops its FIFO, some status flags clear), and a
 * bit-field store reads, modifies and writes the whole register, which is
 * wrong for write-1-to-clear flags. Use whole-cell `!` on such registers.
 */

#define IO_BASE     0x40000000u         /* PERIPH_BASE in ch32h417.h */
#define IO_CELLS    (0x38400u / 4)      /* through UHSIF */

/**
 * Tells whether cell `cell` of the I/O page (an offset in cells from
 * IO_BASE) lies in a peripheral's 1 KB register slot.
 * Returns 1 if it does, else 0.
 */
int vmIoValid(uint32_t cell);

#endif /* IOPAGE_H */
