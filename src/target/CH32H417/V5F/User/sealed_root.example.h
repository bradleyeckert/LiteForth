/*
 * An example of sealed_root.h, which the application's build supplies
 * beside board.c: the root public key that certifies release keys, and
 * the product ID, for checking firmware update packages before stage 1
 * does (`-2 ymodem`; doc/admin/encrypted-blocks.md).
 * `scripts/admin/sealblocks.py root-header ROOT.pub --product P` writes one.
 *
 * Copy it to sealed_root.h (which git ignores) with your own values.
 * Without sealed_root.h the board can't check packages. Never build a
 * product with a root key whose private half is anywhere but offline.
 */
#ifndef SEALED_ROOT_H
#define SEALED_ROOT_H

/* The root's Ed25519 public key, 32 bytes */
#define SEALED_ROOT_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

/* The product ID that release keys and packages name */
#define SEALED_PRODUCT 0x00000000u

/* Where the board's key comes from follows SECURITY_LEVEL in
   Common/security.h, not this file. */

#endif /* SEALED_ROOT_H */
