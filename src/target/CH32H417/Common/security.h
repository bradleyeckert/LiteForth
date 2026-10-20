#ifndef SECURITY_H
#define SECURITY_H

/*
 * SECURITY_LEVEL: one setting for the security of both cores' builds
 * (doc/admin/updates.md section 6, doc/admin/firmware-updates.md section 8,
 * doc/admin/security-plan.md 5.6). Each level adds to the one below it.
 *
 *                               core    0         1         2
 *   Forth reaches peripherals   V5F     all       allowlist allowlist
 *   Block files                 V5F     any       any       sealed only
 *   The board's key (`enroll`)  both    UID       UID       RNG
 *   Board not locked down       V3F     warning   warning   no V5F
 *   Stage 1's `adopt`           V3F     yes       yes       no
 *   Forth starts with           V5F     root      root      root [1]
 *
 * Both cores work out the board's key (V5F/User/board.c, and stage 1's
 * keys_get for packages), so build both projects at the same level.
 *
 * 0, development (the repository's setting): LiteForth boots the regular
 *   Forth from a plain lfblocks.fb4, and Forth reaches every peripheral
 *   register (VM page VM_IO_PAGE maps all of IO_BASE..IO_BASE + 4 *
 *   IO_CELLS). The board's key is BLAKE2s of its UID (personalization
 *   LFinsec1), so `enroll` prints the same line every time, a download
 *   that erases the flash loses nothing, and `sealblocks.py board-key
 *   --insecure` makes the boards.csv line without the board. Anyone who
 *   knows the UID can read what is sealed for it.
 * 1, development with the product's I/O: as 0, but Forth reaches only the
 *   peripherals in V5F/User/io_allow.h: GPIO, timers, serial ports and the
 *   like, not the flash controller, DMA, SDIO, the ECDC, the RNG, RCC/PWR
 *   writes or USB. Anything else reads as an invalid address (-9), and a
 *   write to a read-only one is write-protected (-20).
 * 2, a product: as 1, and the board takes only sealed block files, its
 *   key comes from the RNG and stays in its flash record (`enroll` once,
 *   at provisioning), stage 1 has no `adopt`, and a board that isn't
 *   locked down (read protection, write protection, the boot ROM's
 *   downloads) doesn't start the V5F: it stays in recovery, which takes
 *   only `reset`, and the debugger can still reprogram it.
 *
 * [1] Level 2 is to start Forth with a limited security wordlist instead
 *     of root. Not yet: it starts with root, as the others do.
 *
 * Moving a board to level 2 changes its key: run `enroll`, put its new
 * line in boards.csv, and seal its files again.
 *
 * Set it here, or with -DSECURITY_LEVEL=n in both MounRiver projects.
 */

#ifndef SECURITY_LEVEL
#define SECURITY_LEVEL      0
#endif

#if (SECURITY_LEVEL < 0) || (SECURITY_LEVEL > 2)
#error "SECURITY_LEVEL must be 0, 1 or 2 (Common/security.h)"
#endif

/* What the level means, for the code (the table above) */
#define SEC_IO_ALLOWLIST    (SECURITY_LEVEL >= 1)   /* VM_IO_PAGE through io_allow.h */
#define SEC_SEALED_ONLY     (SECURITY_LEVEL >= 2)   /* plain block files refused */
#define SEC_UID_KEY         (SECURITY_LEVEL <= 1)   /* the board's key from its UID */
#define SEC_STRICT_RDP      (SECURITY_LEVEL >= 2)   /* no V5F on an open board */
#define SEC_ADOPT           (SECURITY_LEVEL <= 1)   /* stage 1's `adopt` and adopt_blank */

#endif /* SECURITY_H */

/*
 * The flags SECURITY_LEVEL replaced. Outside the include guard, so that
 * including this again after sealed_root.h checks that too.
 */
#if defined(DEVELOPER_MODE) || defined(SEALED_ONLY) || defined(INSECURE) || \
    defined(STRICT_RDP) || defined(STAGE1_DEVELOPMENT)
#error "DEVELOPER_MODE, SEALED_ONLY, INSECURE, STRICT_RDP and STAGE1_DEVELOPMENT are gone: set SECURITY_LEVEL (Common/security.h)"
#endif
