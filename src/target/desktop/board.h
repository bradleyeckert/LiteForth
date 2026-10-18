#ifndef BOARD_H
#define BOARD_H

#include "sealed.h"

/*
 * The desktop's stand-in for what a board keeps in protected flash and
 * firmware for sealed block files (doc/encrypted-blocks.md): a text file,
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

#endif /* BOARD_H */
