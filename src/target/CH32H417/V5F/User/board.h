#ifndef BOARD_H
#define BOARD_H

#include "../../../../sealed.h"     /* src/sealed.h */
#include "ecdc.h"                   /* lfAPI_ecdcTest, for api0.c */

/*
 * What the V5F knows about this board, for firmware update packages
 * (doc/admin/encrypted-blocks.md): its UID, and the root public key and
 * product ID, which the application's build supplies in sealed_root.h
 * (see sealed_root.example.h). Not its private key: only stage 1 on the
 * V3F holds that (V3F/User/stage1.c), in its security record, which the
 * V5F's PMP entry 0 keeps the V5F out of. So `-2 ymodem` checks a package
 * without the key (sealed_verify_package), and stage 1 checks the rest.
 */

/**
 * Fills b with the board's UID (ESIG UNIID1 and UNIID2, as they lie in
 * memory), the root key and product ID from sealed_root.h; no private key
 * and no minimums (stage 1 has those). msg receives the reason if it can't.
 * Returns 0, or ERR_UNSUPPORTED_OPERATION if no root key is built in.
 */
int board_get(sealed_board *b, char *msg, int len);

/**
 * Forth word `enroll`  ( -- )
 * Prints the board's line for boards.csv: UID and X25519 public key, in
 * hex, which it asks stage 1 for through the shared block (enroll,
 * board_pub in cdc_shared.h). Below SECURITY_LEVEL 2 the key comes from
 * the UID. At level 2 it comes from stage 1's record; a board without one
 * makes it in recovery (Ctrl+X x3 twice, then enroll), so here it only
 * says so.
 * Returns 0, or ERR_UNSUPPORTED_OPERATION if there's no key yet or stage 1
 * doesn't answer.
 */
int lfAPI_enroll(void);

#endif /* BOARD_H */
