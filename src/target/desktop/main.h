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

#endif /* MAIN_H */
