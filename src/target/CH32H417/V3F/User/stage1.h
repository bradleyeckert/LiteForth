#ifndef STAGE1_H
#define STAGE1_H

/*
 * Stage 1 on the V3F (doc/firmware-updates.md): installing a V5F image
 * from a package that `-2 ymodem` staged in the card's lfstage.bin, with
 * src/update.c, before the V5F starts; and recovery when there's no
 * working V5F image. Not yet: stage 0, the two stage 1 slots, the new
 * flash layout, a recovery install with no card. So for now:
 *
 * - It does nothing at all on a board that has never been updated: no
 *   security record and no package waiting. Then the V3F boots as before.
 * - In a development build (STAGE1_DEVELOPMENT), a blank record takes the
 *   V5F image in flash (its whole 128 KB slot) as installed, version 0
 *   (upd_io.adopt_blank), so that it can be backed up and rolled back to,
 *   and recovery offers `adopt`.
 * - Recovery: when upd_boot finds no working V5F image (UPD_RECOVERY: none
 *   installed, one that fails its MAC, or one that wasn't confirmed, each
 *   with no good backup), the V5F isn't started. Stage 1 answers on the
 *   USB terminal itself, through the rings, with why, and takes
 *   `-2 ymodem` (kwai's ~x FILE -2): a package by YMODEM into
 *   lfstage.bin (src/ymrecv.c), which it then installs and, if that works,
 *   resets the chip to try it, on trial as usual. Also `reset`, and in a
 *   development build `adopt` (upd_adopt: the image in flash as it is,
 *   for a V5F reprogrammed by the debugger).
 *
 * It needs the card's lfstage.bin (for the package) and lfbackup.bin (at
 * least 33 blocks, 132 KB, for the old image), both made in a PC; the
 * root key and product from V5F/User/sealed_root.h; and the board's key
 * from the V5F's board record (0x08070000) or, in an INSECURE build, from
 * the UID, as V5F/User/board.c has them.
 */

#ifndef STAGE1_DEVELOPMENT
#define STAGE1_DEVELOPMENT  1           /* 0 for a production build */
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
 * From the V3F's main loop. Normally: when the V5F says it started
 * (`update` is CDC_CONFIRM_MAGIC) and an image is on trial, ends the trial
 * (upd_confirm); cheap when there's nothing to do. In recovery: the
 * recovery terminal, one byte at a time, except while a package comes in.
 */
void stage1_poll(void);

#endif /* STAGE1_H */
