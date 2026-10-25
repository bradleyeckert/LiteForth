#ifndef IOMAP_H
#define IOMAP_H

#ifndef __ASSEMBLER__                  /* vm_rv32.S needs only the indexes */
#include <stdint.h>
#endif

/*
 * The VM's I/O space: what a chip that implements the VM in hardware
 * would wire to its `>sys` and `sys>` instructions. Indexes below
 * IOMAP_FIRST are the VM's own (VMSTO_* and VMSFROM_* in vm_labels.h);
 * from IOMAP_FIRST up, `>sys` ( x -- ) hands T to iomap_wr and `sys>`
 * ( -- x ) pushes what iomap_rd gives. Reads and writes are separate
 * spaces, so one index can be a read port and a write port, as a UART's
 * data register is. A port that fails returns an ior, which the VM
 * treats like a failed memory access: a word the terminal runs stops
 * with it, a stepped app goes to its yeet handler.
 * 
 * The `>sys` and `sys>` are 16-bit instructions with 9-bit immediate data,
 * so the I/O space is 512 ports. Due to the immediate data, they are compile-only.
 * The syntax is `[ 16 >sys ]` or `[ 16 sys> ]` for port 16, etc.
 *
 * Terminal (the console's serial port) examples:
 *   : key    ( -- c )    [ 16 sys> ] ; \ waits for a character
 *   : emit   ( c -- )	  [ 16 >sys ] ; \ outputs a character
 *   : key?   ( -- flag ) [ 17 sys> ] ; \ nonzero if a character is waiting
 *   : emit?  ( -- flag ) [ 18 sys> ] ; \ nonzero while the transmitter is busy
 *
 * Math unit: two argument registers A0 A1 and up to three results Y0 Y1 Y2.
 * Each are individually addressable (An for argument, Yn for a result).
 * The unit doesn't block the VM: IO_MATH_STATUS reads nonzero while it's busy,
 * and 0 once the results are ready. While it's busy, results are indeterminate.
 * 
 * Starting mu/mod or star-slash-mod with a divisor of 0 fails with -10 and starts nothing.
 * Reading a result while the unit is busy fails with -21.
 * The registers are shared: while one word is using the unit, no other may start
 * an operation, so a word that pauses while it waits must own the unit for that time.
 */

#define IOMAP_FIRST         16      /* the first I/O index; 0-15 are the VM's */

/* Terminal */
#define IO_TERM_DATA        16      /* sys>: key, >sys: emit */
#define IO_TERM_RXQ         17      /* sys>: key? */
#define IO_TERM_TXQ         18      /* sys>: emit? */

/* Math unit */
#define IO_MATH_UMSTAR      32      /* >sys: um*: A0 * tos unsigned, Y1:Y0 result */
#define IO_MATH_MSTAR       33      /* >sys: m*: A0 * tos signed, Y1:Y0 result */
#define IO_MATH_MUDIVMOD    34      /* >sys: mu/mod: A0:tos / A1, remainder Y2, quotient Y1:Y0 */
#define IO_MATH_STARDIVMOD  35      /* >sys: star-slash-mod: A0 * tos / A1 symmetric, remainder Y2, quotient Y0 */
#define IO_MATH_ARG         40      /* >sys: store to argument A0 (add 1 for A1) */
#define IO_MATH_RES         40      /* sys>: read from result Y0 (add 1 for Y1, 2 for Y2) */
#define IO_MATH_STATUS      43      /* sys>: nonzero while busy, 0 when the results are ready */

#ifndef __ASSEMBLER__
/**
 * Reads I/O port addr, for `sys>`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param tos Receives the value for T, or 0 if the read fails.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be read,
 * ERR_UNSUPPORTED_OPERATION for a math result while the unit is busy, or
 * the terminal's error.
 */
int iomap_rd(int addr, uint32_t *tos);

/**
 * Writes data to I/O port addr, for `>sys`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param tos The value, from T.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be
 * written, ERR_DIVISION_BY_ZERO when the math unit is started with a
 * divisor of 0, or the terminal's error.
 */
int iomap_wr(int addr, uint32_t tos);

/**
 * For tests: how many reads of IO_MATH_STATUS say busy after an operation
 * starts, as a slower unit would (0, the default: none).
 */
extern int iomap_math_latency;

#endif /* __ASSEMBLER__ */

#endif /* IOMAP_H */
