#ifndef PMP_H
#define PMP_H

#include <stdint.h>

/*
 * The V5F's PMP (doc/firmware-updates.md section 8): four locked entries
 * that keep the whole V5F, its machine-mode trap handlers included, away
 * from stage 1's security record, which holds the device token. The V5F
 * never needs it; only the V3F does.
 *
 *   0  0x00070000..0x00077FFF  none   the code flash's low window over the
 *                                     records (the V5F reads that window only
 *                                     in its start-up copy, before this)
 *   1  0x08072000..0x08073FFF  none   record A (UPD_RECORD_A)
 *   2  0x08074000..0x08075FFF  none   record B (UPD_RECORD_B)
 *   3  all memory              RWX    everything else, as before
 *
 * Locked entries apply to machine mode too, and only a reset clears them.
 * The V5F's own board record at 0x08070000 stays readable and writable
 * (board.c). PMP doesn't stop DMA, or the flash controller erasing a
 * sector by address: those still need fencing.
 */

/**
 * Sets and locks the entries above. Called by the start-up code
 * (startup_ch32h417_v5f.S) in machine mode, just before its `mret` into
 * `main` (which runs in user mode, where the PMP CSRs can't be reached).
 * Then tries to clear entry 0 and keeps what the CSRs read back, for
 * pmp_report. Touches no memory but its own variables and the stack.
 */
void pmp_lock(void);

/**
 * Prints on the debug port what pmp_lock set and read back:
 * `V5F PMP: cfg0 ... addr ... ...`, and whether the entries stayed locked
 * when it tried to clear entry 0.
 */
void pmp_report(void);

#endif /* PMP_H */
