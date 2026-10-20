#ifndef SEALED_H
#define SEALED_H

#include <stdint.h>

/*
 * Sealed block files (doc/admin/encrypted-blocks.md): a block file whose content
 * blocks are encrypted (AES-128 or SM4 in CTR mode) for a list of boards,
 * with BLAKE2s hashes of the encrypted blocks in hash table blocks and a
 * block 0 signed by a release key that the root key certifies.
 * scripts/admin/sealblocks.py makes them. The targets' blocks.c call
 * sealed_mount when block 0 starts with SEALED_MAGIC, and sealed_open_block
 * on every content block they read.
 */

#define SEALED_MAGIC        "LITEFORTHS"
#define SEALED_MAGIC_LEN    10
/* A firmware update package (doc/admin/firmware-updates.md section 5): a sealed
   file whose content is a firmware image, not blocks. Never mounted as a
   block file; `-1 ymodem` refuses it. Also SEALED_MAGIC_LEN long. */
#define PACKAGE_MAGIC       "LITEFORTHF"
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
                                       block files (SECURITY_LEVEL 2); sealed.c
                                       doesn't look at it */
    /* If x25519 is set, it holds the private key, and private_key can stay
       zero: sealed.c calls it for the shared secret with a file's ephemeral
       public key (returning 0, or nonzero if it can't), and takes the
       board's public key from public_key. The CH32H417's V5F sets it, so
       its key stays in machine mode (V5F/User/board.c). */
    int (*x25519)(uint8_t shared[32], const uint8_t their_public[32]);
    uint8_t public_key[32];         /* X25519, with x25519 */
} sealed_board;

/* A firmware update package's description (doc/admin/firmware-updates.md
   section 5), from block 0 */
enum { SEALED_V5F = 1, SEALED_STAGE1 = 2 };
typedef struct {
    uint32_t component;             /* SEALED_V5F or SEALED_STAGE1 */
    uint32_t hw_rev;                /* 0: any */
    uint32_t length;                /* the image's bytes */
    uint32_t min_version;           /* the new minimum, or 0 */
    uint32_t version;               /* the revision field: the image's version */
    uint8_t image_hash[32];         /* BLAKE2s(image, 32, person "LFimage") */
} sealed_package;

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
 * sealed_mount for a firmware update package (PACKAGE_MAGIC): the same
 * checks (board->min_revision is then the lowest version the board takes
 * for this kind of image), with block 0's description, which fills pkg.
 * Its content blocks, 1 to sealed_blocks() - 1, are the image, padded,
 * read with sealed_open_block. The image hash isn't checked here: it's
 * of the decrypted image, so the caller checks it as it reads the blocks.
 * Returns 0 or ERR_BLK_PARSE_FAIL.
 */
int sealed_mount_package(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, char *msg, int len, sealed_package *pkg);

/**
 * Checks a whole package as stage 1 will before installing it, except the
 * board's minimum version: sealed_mount_package (with board->min_revision
 * ignored), then every content block read with `read` into buf (4096
 * bytes, 4-byte aligned) and checked against its hash, and the decrypted
 * image against the hash in block 0. Unmounts it afterwards (and so
 * whatever was mounted before). msg receives what it found or why it
 * failed; pkg its description.
 * Returns 0, ERR_BLK_PARSE_FAIL, ERR_BLOCK_READ_ERROR, or read's error.
 */
int sealed_check_package(uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, uint8_t *buf, sealed_package *pkg,
                         char *msg, int len);

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
