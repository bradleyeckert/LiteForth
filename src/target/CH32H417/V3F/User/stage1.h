#ifndef STAGE1_H
#define STAGE1_H

/*
 * Stage 1 on the V3F (doc/firmware-updates.md), first step: installing a
 * V5F image from a package that `-2 ymodem` staged in the card's
 * lfstage.bin, with src/update.c, before the V5F starts. Not yet: stage 0,
 * the two stage 1 slots, recovery over USB, the new flash layout. So for
 * now:
 *
 * - It does nothing at all on a board that has never been updated: no
 *   security record and no package waiting. Then the V3F boots as before.
 * - A blank record takes the V5F image in flash (its whole 128 KB slot) as
 *   installed, version 0 (upd_io.adopt_blank), so that it can be backed up
 *   and rolled back to.
 * - If the V5F image fails its check (UPD_RECOVERY) it says so on the
 *   debug UART and starts the V5F anyway: there's no recovery mode yet.
 *
 * It needs the card's lfstage.bin (for the package) and lfbackup.bin (at
 * least 33 blocks, 132 KB, for the old image), both made in a PC; the
 * root key and product from V5F/User/sealed_root.h; and the board's key
 * from the V5F's board record (0x08070000) or, in an INSECURE build, from
 * the UID, as V5F/User/board.c has them.
 */

/**
 * At reset, before the V5F is woken: if a package is waiting (`update` in
 * the shared block is CDC_UPDATE_MAGIC) or the security record exists,
 * runs upd_boot: carries on with any update under way, or starts one, and
 * checks the V5F image. Clears the flag. Reports on the debug UART.
 */
void stage1_boot(void);

/**
 * From the V3F's main loop: when the V5F says it started (`update` is
 * CDC_CONFIRM_MAGIC) and an image is on trial, ends the trial
 * (upd_confirm). Cheap when there's nothing to do.
 */
void stage1_poll(void);

#endif /* STAGE1_H */
