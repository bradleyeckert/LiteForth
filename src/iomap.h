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
 * Terminal (the console's serial port):
 *   16  sys>  key    ( -- c )      waits for a character
 *       >sys  emit   ( c -- )
 *   17  sys>  key?   ( -- flag )   nonzero if a character is waiting
 *   18  sys>  emit?  ( -- flag )   nonzero while the transmitter is busy
 *
 * Math unit: three argument registers A0 A1 A2 and up to three results.
 * Writing IO_MATH_ARG shifts an argument in (A2 <- A1 <- A0 <- x).
 * Writing an operation's port shifts in its last argument the same way
 * and starts it, so a word writes its arguments from the top of the
 * stack down and A0 holds the deepest. The unit doesn't block the VM:
 * IO_MATH_STATUS reads nonzero while it's busy, and 0 once the results
 * are ready (as a hardware unit with iterative multiply and divide
 * would; this one finishes at once). IO_MATH_RES then gives the results
 * in order, so they land on the stack as the word leaves them:
 *   um*     ( u1 u2 -- ud )            32 >sys 33 >sys wait 32 sys> 32 sys>
 *   m*      ( n1 n2 -- d )             32 >sys 34 >sys wait 32 sys> 32 sys>
 *   mu/mod  ( udl udh u -- r uql uqh ) 32 >sys 32 >sys 35 >sys wait 32 sys> 32 sys> 32 sys>
 *   star-slash-mod ( n1 n2 n3 -- r q ) 32 >sys 32 >sys 36 >sys wait 32 sys> 32 sys>
 * where wait is `begin 37 sys> 0= until` (or `pause` in the loop, to let
 * other tasks run meanwhile). Starting mu/mod or star-slash-mod with a
 * divisor of 0 fails with -10 and starts nothing. Reading a result while
 * the unit is busy fails with -21. The registers are shared: while one
 * word is using the unit, no other may start an operation, so a word that
 * pauses while it waits must own the unit for that time.
 */

#define IOMAP_FIRST         16      /* the first I/O index; 0-15 are the VM's */

/* Terminal */
#define IO_TERM_DATA        16      /* sys>: key, >sys: emit */
#define IO_TERM_RXQ         17      /* sys>: key? */
#define IO_TERM_TXQ         18      /* sys>: emit? */

/* Math unit */
#define IO_MATH_ARG         32      /* >sys: shift in an argument */
#define IO_MATH_RES         32      /* sys>: the next result, 0 when none is left */
#define IO_MATH_UMSTAR      33      /* >sys: last argument, start um*: A0 * A1 unsigned, low cell then high */
#define IO_MATH_MSTAR       34      /* >sys: last argument, start m*: signed */
#define IO_MATH_MUDIVMOD    35      /* >sys: last argument, start mu/mod: A1:A0 / A2, remainder, quotient low, high */
#define IO_MATH_STARDIVMOD  36      /* >sys: last argument, start star-slash-mod: A0 * A1 / A2 symmetric, remainder, quotient */
#define IO_MATH_STATUS      37      /* sys>: nonzero while busy, 0 when the results are ready */

#ifndef __ASSEMBLER__
/**
 * Reads I/O port addr, for `sys>`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param data Receives the value, or 0 if the read fails.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be read,
 * ERR_UNSUPPORTED_OPERATION for a math result while the unit is busy, or
 * the terminal's error.
 */
int iomap_rd(int addr, uint32_t *data);

/**
 * Writes data to I/O port addr, for `>sys`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param data The value.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be
 * written, ERR_DIVISION_BY_ZERO when the math unit is started with a
 * divisor of 0, or the terminal's error.
 */
int iomap_wr(int addr, uint32_t data);

/**
 * For tests: how many reads of IO_MATH_STATUS say busy after an operation
 * starts, as a slower unit would (0, the default: none).
 */
extern int iomap_math_latency;

#endif /* __ASSEMBLER__ */

#endif /* IOMAP_H */
