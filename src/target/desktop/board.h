#ifndef BOARD_H
#define BOARD_H

#include "sealed.h"

/*
 * The desktop's stand-in for what a board keeps in protected flash and
 * firmware for checking firmware update packages (doc/admin/encrypted-blocks.md): a text file,
 * given to lf with -K, that `sealblocks.py lf-board` writes. One setting
 * per line, `#` starts a comment:
 *
 *   uid 0102030405060708        8 bytes, hex
 *   private <64 hex digits>     the board's X25519 private key
 *   root <64 hex digits>        the root's Ed25519 public key
 *   product 0x4C46              the product ID (C number syntax)
 *   min-revision 0              optional, default 0
 *   min-serial 0                optional, default 0
 */

/**
 * Reads a board file into b. msg receives the reason if it fails.
 * Returns 0, or ERR_BLK_PARSE_FAIL if the file can't be read or a setting
 * is missing or malformed.
 */
int board_load(const char *path, sealed_board *b, char *msg, int len);

/**
 * Forth word `enroll`  ( -- )
 * Prints the board's line for boards.csv (UID and X25519 public key, in
 * hex), from the board file lf was given with -K, as the CH32H417's
 * `enroll` does from its flash record. The desktop never makes a key: the
 * board file holds it (sealblocks.py board-key, lf-board).
 * Returns 0, or ERR_UNSUPPORTED_OPERATION without -K.
 */
int lfAPI_enroll(void);

/**
 * Forth word `ecdc-test`  ( -- )
 * Tests the CH32H417's AES/SM4 engine (V5F/User/ecdc.h). The desktop has
 * none: it says so.
 * Returns ERR_UNSUPPORTED_OPERATION.
 */
int lfAPI_ecdcTest(void);

#endif /* BOARD_H */
