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
 * Writing IO_MATH_ARG shifts an argument in (A2 <- A1 <- A0 <- x), so
 * after a word's arguments are written from the top of the stack down,
 * A0 holds the deepest. Reading an operation's port computes it from the
 * arguments and gives its first result; IO_MATH_RES then gives the
 * others, in order, so they land on the stack as the word leaves them:
 *   um*     ( u1 u2 -- ud )            32 >sys 32 >sys 33 sys> 32 sys>
 *   m*      ( n1 n2 -- d )             32 >sys 32 >sys 34 sys> 32 sys>
 *   mu/mod  ( udl udh u -- r uql uqh ) 32 >sys 32 >sys 32 >sys 35 sys> 32 sys> 32 sys>
 *   star-slash-mod ( n1 n2 n3 -- r q )     32 >sys 32 >sys 32 >sys 36 sys> 32 sys>
 * mu/mod and star-slash-mod give -10 (division by zero) if the divisor is 0. The
 * registers are shared: a sequence must not be split by `pause` (none of
 * these are, as they're straight-line code).
 */

#define IOMAP_FIRST         16      /* the first I/O index; 0-15 are the VM's */

/* Terminal */
#define IO_TERM_DATA        16      /* sys>: key, >sys: emit */
#define IO_TERM_RXQ         17      /* sys>: key? */
#define IO_TERM_TXQ         18      /* sys>: emit? */

/* Math unit */
#define IO_MATH_ARG         32      /* >sys: shift in an argument */
#define IO_MATH_RES         32      /* sys>: the operation's next result, 0 when none is left */
#define IO_MATH_UMSTAR      33      /* sys>: um*, A0 * A1 unsigned: low cell, then high */
#define IO_MATH_MSTAR       34      /* sys>: m*, signed */
#define IO_MATH_MUDIVMOD    35      /* sys>: mu/mod, A1:A0 / A2: remainder, quotient low, high */
#define IO_MATH_STARDIVMOD  36      /* sys>: star-slash-mod, A0 * A1 / A2 symmetric: remainder, quotient */

#ifndef __ASSEMBLER__
/**
 * Reads I/O port addr, for `sys>`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param data Receives the value, or 0 if the read fails.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be read,
 * ERR_DIVISION_BY_ZERO from the math unit, or the terminal's error.
 */
int iomap_rd(int addr, uint32_t *data);

/**
 * Writes data to I/O port addr, for `>sys`.
 * @param addr The port, IOMAP_FIRST to 511.
 * @param data The value.
 * @return 0, or an ior: ERR_INVALID_ADDRESS for a port that can't be
 * written, or the terminal's error.
 */
int iomap_wr(int addr, uint32_t data);

#endif /* __ASSEMBLER__ */

#endif /* IOMAP_H */
