#ifndef BOARD_H
#define BOARD_H

#include "../../../../sealed.h"     /* src/sealed.h */

/*
 * What this board keeps for sealed block files (doc/encrypted-blocks.md):
 * its X25519 key and the two minimums, in a record in the 8 KB flash
 * sector right after LiteForth's flash pages (0x08070000), and the root
 * public key and product ID, which the application's build supplies in
 * sealed_root.h (see sealed_root.example.h). Without sealed_root.h the
 * board reads no sealed files.
 */

/**
 * Fills b with what the board holds: its UID (ESIG UNIID1 and UNIID2, as
 * they lie in memory), its private key, the root key and product ID from
 * sealed_root.h, and its minimums. msg receives the reason if it can't.
 * Returns 0, or ERR_UNSUPPORTED_OPERATION if there is no root key built
 * in or the board hasn't been enrolled.
 */
int board_get(sealed_board *b, char *msg, int len);

/**
 * Forth word `enroll`  ( -- )
 * Makes the board's X25519 key pair from the RNG and stores the private
 * key in the flash record, unless it has one already; then prints the
 * board's line for boards.csv: UID and public key, in hex. Run once per
 * board at provisioning. The key never leaves the board.
 * Returns 0, ERR_FLASH_WRITE_SECTOR if the record can't be written, or
 * ERR_UNSUPPORTED_OPERATION if the RNG fails its checks.
 */
int lfAPI_enroll(void);

#endif /* BOARD_H */
