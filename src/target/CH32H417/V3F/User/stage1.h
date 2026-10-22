#ifndef STAGE1_H
#define STAGE1_H

/*
 * Stage 1 on the V3F (doc/admin/firmware-updates.md): installing a V5F image
 * from a package that `-2 ymodem` staged in the card's lfstage.bin, with
 * src/update.c, before the V5F starts; and recovery when there's no
 * working V5F image. Not yet: stage 0, the two stage 1 slots, the new
 * flash layout. So for now:
 *
 * - It does nothing at all on a board that has never been updated: no
 *   security record and no package waiting. Then the V3F boots as before,
 *   below SECURITY_LEVEL 2; at 2 (SEC_SIGNED_BOOT) that's recovery, as
 *   is a record it can't read or a board with no key yet, since only an
 *   image that passes the check may start.
 * - Below SECURITY_LEVEL 2 (SEC_ADOPT), a blank record takes the
 *   V5F image in flash (its whole 128 KB slot) as installed, version 0
 *   (upd_io.adopt_blank), so that it can be backed up and rolled back to,
 *   and recovery offers `adopt`.
 * - Recovery: when upd_boot finds no working V5F image (UPD_RECOVERY: none
 *   installed, one that fails its MAC, or one that wasn't confirmed, each
 *   with no good backup), the V5F isn't started. Stage 1 answers on the
 *   USB terminal itself, through the rings, with why, and takes
 *   `-2 ymodem` (kwai's ~x FILE -2): a package by YMODEM into
 *   lfstage.bin (src/ymrecv.c), which it then installs and, if that works,
 *   resets the chip to try it, on trial as usual. With no lfstage.bin (or
 *   no card) the package goes into RAM and installs from there. Also
 *   `reset`, and below level 2 `adopt` (upd_adopt: the image in
 *   flash as it is, for a V5F reprogrammed by the debugger).
 * - Recovery on request: Ctrl+X x3 resets the chip, the V5F starting
 *   without the app; if it's still stuck before LiteForth when Ctrl+X x3
 *   comes again, the V3F resets the chip into recovery (`recover` in cdc_shared.h). A package
 *   sent then replaces the stuck image (upd_mark_broken first, so it
 *   needs no backup); `reset` just tries the V5F again.
 * - A V5F that doesn't start at all (stage1_wait_v5f) makes the V3F reset
 *   the chip after STAGE1_V5F_START_MS, so a trial runs out by itself, and
 *   after STAGE1_START_TRIES such boots in a row stage 1 goes to recovery.
 * - The lock-down check, at every boot: read protection on, the V3F's
 *   sectors write-protected and the rest writable (stage1_layout.h), and
 *   the boot ROM's USB and USART downloads off. Anything else is one
 *   "V3F security: not locked down: ..." line on the debug UART. At
 *   SECURITY_LEVEL 2 (SEC_STRICT_RDP) such a board also doesn't start the
 *   V5F: recovery says why and takes only `reset` and `lockdown`, which
 *   programs the option bytes as the check wants them (one erase and one
 *   pass, the values WCH's library would write), reads them back and
 *   resets; only a chip erase undoes it. scripts/admin/lockdown.py flashes
 *   a board and drives this.
 *
 * It needs the card's lfstage.bin (for the package) and lfbackup.bin (at
 * least 29 blocks, 116 KB, for the old image), both made in a PC; the
 * root key and product from V5F/User/sealed_root.h; and the board's key,
 * which only stage 1 holds: one of the device token's keys
 * (upd_board_key) or, below SECURITY_LEVEL 2, from the UID. Recovery's
 * `enroll` prints the board's boards.csv line (at level 2 making the
 * record, and so the key, if there's none), and so does Forth's `enroll`,
 * through the shared block (serve_enroll).
 */

#include "security.h"                   /* SECURITY_LEVEL (Common/) */

#ifndef STAGE1_V5F_START_MS
#define STAGE1_V5F_START_MS 3000        /* stage1_wait_v5f: then the chip resets; 0 = wait for good */
#endif
#ifndef STAGE1_START_TRIES
#define STAGE1_START_TRIES  3           /* boots in a row the V5F may fail to start: then recovery */
#endif

#define STAGE1_START        0           /* stage1_boot: wake the V5F */
#define STAGE1_RECOVERY     1           /* don't: recovery, in stage1_poll */

/**
 * At reset, before the V5F is woken: if a package is waiting (`update` in
 * the shared block is CDC_UPDATE_MAGIC) or the security record exists,
 * runs upd_boot: carries on with any update under way, or starts one, and
 * checks the V5F image. Clears the flag. Reports on the debug UART.
 * Returns STAGE1_START, or STAGE1_RECOVERY if there's no working V5F
 * image: then the caller doesn't wake the V5F, and stage1_poll runs the
 * recovery terminal.
 */
int stage1_boot(void);

/**
 * After the V3F woke the V5F: waits up to STAGE1_V5F_START_MS for the V5F
 * to release HSEM0, as its main() does first. If it doesn't, the image
 * doesn't start at all: counts the boot in `nostart` (cdc_shared.h) and
 * resets the chip, so an image on trial uses up its trial boots by itself
 * and is rolled back, and an installed one that stops starting gets
 * recovery after STAGE1_START_TRIES boots, with nobody pressing reset.
 * Returns once the V5F has started (and clears the count). A debugger
 * holding the V5F at its start counts too: set STAGE1_V5F_START_MS to 0
 * for that.
 */
void stage1_wait_v5f(void);

/**
 * From the V3F's main loop. Normally: when the V5F says it started
 * (`update` is CDC_CONFIRM_MAGIC) and an image is on trial, ends the trial
 * (upd_confirm); cheap when there's nothing to do. In recovery: the
 * recovery terminal, one byte at a time, except while a package comes in.
 */
void stage1_poll(void);

#endif /* STAGE1_H */
