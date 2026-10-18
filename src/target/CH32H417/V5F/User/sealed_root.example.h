/*
 * An example of sealed_root.h, which the application's build supplies
 * beside board.c: the root public key that certifies release keys, and
 * the product ID, for sealed block files (doc/encrypted-blocks.md).
 * `scripts/sealblocks.py root-header ROOT.pub --product P` writes one.
 *
 * Copy it to sealed_root.h (which git ignores) with your own values.
 * Without sealed_root.h the board reads no sealed files. Never build a
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

/* The product ID that release keys and sealed files name */
#define SEALED_PRODUCT 0x00000000u

/* Optional: 1 makes the board refuse plain block files, so it runs only
   sealed releases: a plain lfblocks.fb4 offers no blocks, and `-1 ymodem`
   takes only sealed files. Leave it out while developing. */
/* #define SEALED_ONLY 1 */

#endif /* SEALED_ROOT_H */
