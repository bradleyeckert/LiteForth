#ifndef MAIN_H
#define MAIN_H

/**
 * Target-specific startup hooks called by the portable Forth core.
 * Each target provides its own main.c and main.h.
 */

/**
 * Sets the origin and limit of the udata, idata, code and text spaces,
 * and the initial idata pointer, in the RAM page's pointer table.
 * Called by forth.c when the dictionary is (re)initialized.
 * Returns 0 if okay, or ERR_ALLOCATE_FAILED if the RAM page is not allocated.
 */
int lfInitPointers(void);

/**
 * Restarts LiteForth as a reset would on an MCU: the system options are put
 * back to what main() was started with, the memory map is set up again and
 * lfQuit starts from the top (booting from flash again if -o 8 asked for
 * it). The flash and block files and the terminal stay open, and the RAM
 * page keeps its contents. Used by lfStandby (lftime.c). Doesn't return.
 */
void lfReset(void);

#endif /* MAIN_H */
