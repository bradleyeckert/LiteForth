#ifndef PMPTEST_H
#define PMPTEST_H

/*
 * pmp-test: what the V5F's PMP and memory map allow, for the design in
 * doc/firmware-updates.md (sections 8 and 10). Nothing it does lasts past
 * a reset: it only reads flash and RAM, and its one locked PMP entry
 * covers a 32-byte buffer of its own, which nothing else uses.
 *
 * LiteForth runs in user mode (WCH's start-up code sets mstatus 0x6088,
 * MPP = 0, before its mret into main), where the PMP CSRs can't be
 * touched, so the word makes an ecall and HardFault_Handler, in machine
 * mode, calls pmp_test_m.
 */

/**
 * Forth word `pmp-test`  ( -- )
 * Makes the ecall, then prints what pmp_test_m found.
 * Returns 0.
 */
int lfAPI_pmpTest(void);

/**
 * The machine-mode part, called by HardFault_Handler for an ecall from
 * user mode (mcause 8), with interrupts off. With its own trap handler in
 * mtvec (mtvec, mepc and mstatus are put back after), so access faults
 * are counted instead of resetting the chip:
 * 1. Notes mstatus.MPP (0: LiteForth runs in user mode) and the PMP
 *    registers; stops if entry 2 or 3 is in use (a second run before a
 *    reset finds entry 2 locked).
 * 2. Compares the code flash's two windows, 0x00000000 and 0x08000000,
 *    at the V3F's code (0x0) and the V5F's (0x10000).
 * 3. Reads the V3F's RAM at 0x20100000.
 * 4. Calls a `c.ret` found in the V3F's flash.
 * 5. Entry 3: all memory, RWX, so user mode keeps working. Entry 2: the
 *    test buffer, no access, unlocked. Machine-mode and user-mode (MPRV)
 *    loads and stores of the buffer, and a user-mode load elsewhere with
 *    and without entry 3 (does a user access that matches no entry fail,
 *    as the RISC-V spec says?).
 * 6. Entry 2 locked: machine-mode load and store of the buffer, then an
 *    attempt to clear it, read back.
 * Leaves entry 3 as all memory RWX, and entry 2 locked, until reset.
 */
void pmp_test_m(void);

#endif /* PMPTEST_H */
