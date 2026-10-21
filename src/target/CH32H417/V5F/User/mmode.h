#ifndef MMODE_H
#define MMODE_H

/*
 * Machine mode's code lives in flash, where user mode can't write it
 * (doc/admin/firmware-updates.md section 8): Link_v5f.ld's .mmode, at the
 * end of the V5F's slot, run in place from the code flash's high window.
 * MMODE puts one function there; whole files (ch32h417_it.c, guard.c,
 * gdma.c) and the vector table go there by name in Link_v5f.ld. Such
 * code must call only other machine-mode code: scripts/mmodecheck.py
 * checks the image (ch32build.sh runs it).
 *
 * WCH's flash routines can't run from flash while they program it (the
 * chip locks up), so they're in .ramfunc: kept in flash, and copied into
 * a corner of ITCM by flash_ram_begin (pmp.c) before each use, with
 * interrupts off, so whatever user mode left there never runs.
 */
#define MMODE __attribute__((section(".mmode")))

#endif /* MMODE_H */
