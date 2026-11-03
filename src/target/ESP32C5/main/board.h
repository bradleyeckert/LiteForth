#ifndef BOARD_H
#define BOARD_H

#include "../../../sealed.h"     /* sealed_board (src/sealed.h) */

/* The board's identity and keys. ESP32-C5, SECURITY_LEVEL 0 (development):
 * the keys/enroll path is stubbed for now; the real root of trust will use
 * the chip's secure boot + flash encryption (doc/ESP32C5/esp32c5-notes.md).
 */

/* Loads a board file into *b (desktop `lf -K`); not used on the board, which
 * holds its own identity. Returns 0 or a negative ior. Stubbed here. */
int board_load(const char *path, sealed_board *b, char *msg, int len);

/* `enroll`: prints the board's boards.csv line (its UID and public key).
 * Stubbed to the eFuse MAC-derived UID at level 0. Returns an ior. */
int lfAPI_enroll(void);

/* `ecdc-test`: checks the hardware crypto engine against software. No such
 * engine is wired up yet on this target; returns 0 (nothing to do). */
int lfAPI_ecdcTest(void);

#endif /* BOARD_H */
