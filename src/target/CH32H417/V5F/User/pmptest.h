#ifndef PMPTEST_H
#define PMPTEST_H

/*
 * pmp-test: what the V5F's PMP and memory map allow, for the design in
 * doc/firmware-updates.md (sections 8 and 10). Nothing it does lasts past
 * a reset: it only reads flash and RAM, and its one locked PMP entry
 * covers a 32-byte buffer of its own, which nothing else uses.
 */

/**
 * Forth word `pmp-test`  ( -- )
 * With interrupts off and its own trap handler in mtvec (restored after),
 * so that access faults are counted instead of resetting the chip:
 * 1. Prints pmpcfg0 and pmpaddr0-3, and stops if entry 3 is in use
 *    (a second run before a reset finds it locked).
 * 2. Compares the code flash's two windows: 0x00000000 against
 *    0x08000000, at the V3F's code (0x0) and the V5F's (0x10000).
 * 3. Reads the V3F's RAM at 0x20100000.
 * 4. Calls a `c.ret` found in the V3F's flash, to see whether the V5F can
 *    execute there.
 * 5. Sets entry 3 over the test buffer, no access, unlocked: a load and a
 *    store in machine mode should not fault. Clears it.
 * 6. Sets it again, locked: the load and store should fault (mcause 5 and
 *    7). Then tries to clear it, and reads it back: a locked entry should
 *    stay until reset.
 * Returns 0.
 */
int lfAPI_pmpTest(void);

#endif /* PMPTEST_H */
