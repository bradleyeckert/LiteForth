#ifndef BOARD_H
#define BOARD_H

#include "../../../../sealed.h"     /* src/sealed.h */
#include "ecdc.h"                   /* lfAPI_ecdcTest, for api0.c */

/*
 * What this board keeps for sealed block files (doc/admin/encrypted-blocks.md):
 * its X25519 key and the two minimums, in a record in the 8 KB flash
 * sector right after LiteForth's flash pages (0x08070000), and the root
 * public key and product ID, which the application's build supplies in
 * sealed_root.h (see sealed_root.example.h). Without sealed_root.h the
 * board reads no sealed files.
 */

/**
 * Fills b with what the board holds: its UID (ESIG UNIID1 and UNIID2, as
 * they lie in memory), its public key and the x25519 hook that reaches
 * its private key in machine mode (b->private_key stays zero), the root
 * key and product ID from sealed_root.h, and its minimums. msg receives the reason if it can't.
 * b->sealed_only is 1 at SECURITY_LEVEL 2 (Common/security.h), else 0,
 * set even when it fails.
 * Returns 0, or ERR_UNSUPPORTED_OPERATION if there is no root key built
 * in or the board hasn't been enrolled (below SECURITY_LEVEL 2 a board needs no
 * record: its key comes from the UID).
 */
int board_get(sealed_board *b, char *msg, int len);

/**
 * Forth word `enroll`  ( -- )
 * Makes the board's X25519 key pair from the RNG (from the UID in an
 * levels 0 and 1) and stores the private key in the flash record, unless
 * it has one already; then prints the
 * board's line for boards.csv: UID and public key, in hex. Run once per
 * board at provisioning. The key never leaves the board.
 * Returns 0, ERR_FLASH_WRITE_SECTOR if the record can't be written, or
 * ERR_UNSUPPORTED_OPERATION if the RNG fails its checks.
 */
int lfAPI_enroll(void);

/* What board_serve tells user mode: never the private key */
typedef struct {
    uint32_t valid;                 /* the record is there and checks */
    uint32_t has_key;               /* the board has a key (always below level 2) */
    uint32_t made;                  /* BOARD_SVC_ENROLL made it just now */
    uint32_t min_revision;
    uint32_t min_serial;
    uint8_t public_key[32];         /* X25519, if has_key */
} board_info;

/* board_serve's services (pmp.c's ecall passes these numbers on) */
enum {
    BOARD_SVC_INFO = 16,            /* a: board_info */
    BOARD_SVC_ENROLL,               /* a: board_info; makes and writes the key
                                       if there's no valid record */
    BOARD_SVC_X25519,               /* a: their public key (32 bytes), b: the
                                       shared secret (32 bytes) */
    BOARD_SVC_LAST = BOARD_SVC_X25519
};

/**
 * MACHINE MODE ONLY: called by pmp.c's ecall handler for services
 * BOARD_SVC_INFO to BOARD_SVC_LAST. Holds everything that needs the
 * private key: reads and checks the record (which user mode can't reach),
 * makes the key (from the RNG, or the UID below level 2) and writes the
 * record, and computes X25519 with it. Buffers (a, b) must lie in DTCM.
 * Wipes the key and the record from its stack before returning.
 * @return 0, ERR_INVALID_ADDRESS, ERR_UNSUPPORTED_OPERATION (no key, or
 * the RNG failed its checks), or ERR_FLASH_WRITE_SECTOR.
 */
int board_serve(uint32_t n, uint32_t a, uint32_t b);

#endif /* BOARD_H */
