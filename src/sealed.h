#ifndef SEALED_H
#define SEALED_H

#include <stdint.h>

/*
 * Sealed block files (doc/encrypted-blocks.md): a block file whose content
 * blocks are encrypted (AES-128 or SM4 in CTR mode) for a list of boards,
 * with BLAKE2s hashes of the encrypted blocks in hash table blocks and a
 * block 0 signed by a release key that the root key certifies.
 * scripts/sealblocks.py makes them. The targets' blocks.c call
 * sealed_mount when block 0 starts with SEALED_MAGIC, and sealed_open_block
 * on every content block they read.
 */

#define SEALED_MAGIC        "LITEFORTHS"
#define SEALED_MAGIC_LEN    10
#define SEALED_BLOCK_BYTES  4096

/* What a board holds: its own key, the root public key, its product, and
   the lowest revision and release key serial it takes */
typedef struct {
    uint8_t uid[8];                 /* ESIG_UNIID1, UNIID2, as in memory */
    uint8_t private_key[32];        /* X25519 */
    uint8_t root[32];               /* Ed25519 public key */
    uint32_t product;
    uint32_t min_revision;
    uint32_t min_serial;
    uint32_t sealed_only;           /* nonzero: the block layer refuses plain
                                       block files (SEALED_ONLY); sealed.c
                                       doesn't look at it */
} sealed_board;

/* Reads raw block blk (4096 bytes) of the file into buf, bypassing
   decryption: for the hash table blocks. buf is 4-byte aligned, so a
   target can read into it by DMA. Returns 0 or an error code. */
typedef int (*sealed_read_fn)(uint32_t blk, uint8_t *buf);

/**
 * Checks block 0 of a sealed file of file_blocks blocks as a board does:
 * its fields, the release key's certificate (with board->root), the
 * release key's serial, block 0's signature, the revision, then finds
 * board->uid among the recipients and unwraps the content key. `read`
 * will read the hash table blocks later. Unmounts any file mounted
 * before. msg receives one line saying what was mounted or why not.
 * Returns 0 if the file is mounted (its content is blocks 1 to
 * sealed_blocks() - 1), or ERR_BLK_PARSE_FAIL.
 */
int sealed_mount(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                 sealed_read_fn read, char *msg, int len);

/**
 * Forgets the mounted file and erases its key.
 */
void sealed_unmount(void);

/**
 * Returns C, the first block after the content of the mounted file (so the
 * readable blocks are 0 to C - 1), or 0 if none is mounted.
 */
uint32_t sealed_blocks(void);

/**
 * Returns the mounted file's revision, or 0.
 */
uint32_t sealed_revision(void);

/**
 * Checks block n, just read from the file into buf, against its hash and
 * decrypts it in place. Block 0 is left as it is. Reads the hash table
 * block that covers n (with the `read` given to sealed_mount) and checks
 * it against block 0, unless it's the one it read last.
 * Returns 0, ERR_BLOCK_READ_ERROR if a hash doesn't match or a crypto
 * engine failed partway (buf is then zeroed), ERR_BLK_BOUNDS if n isn't a content block, or read's error.
 */
int sealed_open_block(uint32_t n, uint8_t *buf);

#endif /* SEALED_H */
