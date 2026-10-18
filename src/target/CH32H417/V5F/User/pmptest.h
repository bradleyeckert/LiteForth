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
 * touched, so the word makes an ecall, and Ecall_U_Mode_Handler (pmptest.c,
 * vector 8), in machine mode, calls pmp_test_m.
 */

/**
 * Forth word `pmp-test`  ( -- )
 * Makes the ecall, then prints what pmp_test_m found.
 * Returns 0.
 */
int lfAPI_pmpTest(void);

/**
 * The machine-mode part, called by Ecall_U_Mode_Handler for an ecall from
 * user mode, with interrupts off. With its own trap handler in
 * mtvec (mtvec, mepc and mstatus are put back after), so access faults
 * are counted instead of resetting the chip:
 * 1. Notes mstatus.MPP (0: LiteForth runs in user mode) and the PMP
 *    registers; stops if entry 2 or 3 is in use (a second run before a
 *    reset finds entry 2 locked).
 * 2. Entry 3: all memory, RWX, so user mode keeps working. Entry 2: the
 *    test buffer, no access, unlocked. Machine-mode and user-mode (MPRV)
 *    loads and stores of the buffer, and a user-mode load elsewhere with
 *    and without entry 3 (does a user access that matches no entry fail,
 *    as the RISC-V spec says?).
 * 3. Entry 3 locked (still all memory, RWX), with a machine-mode load;
 *    then entry 2 locked: machine-mode load and store of the buffer, then
 *    an attempt to clear it, read back.
 * 4. Compares the code flash's two windows, 0x00000000 and 0x08000000,
 *    at the V3F's code (0x0) and the V5F's (0x10000).
 * 5. Reads the V3F's RAM at 0x20100000.
 * 6. Calls a `c.ret` found in the V3F's flash.
 * Everything it says also goes straight to the debug UART, with a mark
 * before each step, so a hang or a lockup shows where it stopped.
 * Leaves entries 2 and 3 locked (3 open: all memory) until reset.
 */
void pmp_test_m(void);

#endif /* PMPTEST_H */
