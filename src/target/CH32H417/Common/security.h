#ifndef SECURITY_H
#define SECURITY_H

/*
 * DEVELOPER_MODE: one switch for the security of both cores' builds
 * (doc/firmware-updates.md section 8, doc/security-plan.md 5.6).
 *
 * 1, a development build: LiteForth boots as it always has, the regular
 * Forth from a plain lfblocks.fb4 (`1 load` and the rest), and nothing
 * stands in the way of working on the board:
 *   - Forth reaches every peripheral register (VM page VM_IO_PAGE maps
 *     all of IO_BASE..IO_BASE + 4 * IO_CELLS);
 *   - plain block files load, unless sealed_root.h defines SEALED_ONLY;
 *   - INSECURE in sealed_root.h is allowed (the board key from the UID);
 *   - stage 1 is a development build (STAGE1_DEVELOPMENT: `adopt`, and a
 *     blank record takes the image in flash), and it only warns about a
 *     board that isn't locked down.
 *
 * 0, a product: the security is all on.
 *   - Forth reaches only the peripherals in the V5F's allowlist
 *     (V5F/User/io_allow.h): GPIO, timers, serial ports and the like, but
 *     not the flash controller, DMA, SDIO, the ECDC, the RNG, RCC/PWR, USB
 *     or the bus masters. Anything else reads as an invalid address (-9),
 *     and a write to a read-only one is write-protected (-20);
 *   - plain block files are refused (SEALED_ONLY): only sealed releases
 *     run;
 *   - INSECURE stops the build;
 *   - stage 1 has no `adopt` (STAGE1_DEVELOPMENT 0), and STRICT_RDP: a
 *     board that isn't locked down (read protection, write protection,
 *     the boot ROM's downloads) doesn't start the V5F. So a product build
 *     runs only on a locked board; on an open one it stays in recovery,
 *     and the debugger can still reprogram it.
 *
 * A build can still set the parts by themselves: SEALED_ONLY in
 * sealed_root.h, and STAGE1_DEVELOPMENT and STRICT_RDP before stage1.h.
 */

#ifndef DEVELOPER_MODE
#define DEVELOPER_MODE      1           /* 0 for a product */
#endif

#endif /* SECURITY_H */
