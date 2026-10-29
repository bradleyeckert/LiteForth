#ifndef SEALED_H
#define SEALED_H

#include <stdint.h>
#include <string.h>

/*
 * The sealed format (doc/admin/encrypted-blocks.md): blocks encrypted
 * (AES-128 or SM4 in CTR mode) for a list of boards, with BLAKE2s hashes of
 * the encrypted blocks in hash table blocks and a block 0 signed by a
 * release key that the root key certifies. scripts/admin/sealblocks.py
 * makes them. Firmware update packages use it (PACKAGE_MAGIC): stage 1
 * opens them (src/update.c). Sealed block files (SEALED_MAGIC) are
 * retired: update packages carry a Forth application now (component 3),
 * and block files are plain. Neither is ever mounted as a block file
 * (sealed_not_blocks).
 */

#define SEALED_MAGIC        "LITEFORTHS"
#define SEALED_MAGIC_LEN    10
/* A firmware update package (doc/admin/firmware-updates.md section 5): a sealed
   file whose content is a firmware image, not blocks. Never mounted as a
   block file; `-1 ymodem` refuses it. Also SEALED_MAGIC_LEN long. */
#define PACKAGE_MAGIC       "LITEFORTHF"
#define SEALED_BLOCK_BYTES  4096

/**
 * Whether block 0 of a would-be block file is sealed, and so not to be
 * mounted or written: a firmware update package, or a sealed block file,
 * which LiteForth no longer reads. Both start with the plain signature
 * "LITEFORTH" too, so a block file's check must ask this first.
 * @return NULL for anything else, or why it isn't a block file.
 */
static inline const char *sealed_not_blocks(const uint8_t *b0) {
    if (memcmp(b0, PACKAGE_MAGIC, SEALED_MAGIC_LEN) == 0) {
        return "a firmware update package, not a block file";
    }
    if (memcmp(b0, SEALED_MAGIC, SEALED_MAGIC_LEN) == 0) {
        return "a sealed block file: no longer read (an app goes in an update package now)";
    }
    return NULL;
}

/* What a board holds: its own key, the root public key, its product, and
   the lowest version and release key serial it takes */
typedef struct {
    uint8_t uid[8];                 /* ESIG_UNIID1, UNIID2, as in memory */
    uint8_t private_key[32];        /* X25519; not needed by sealed_verify_package */
    uint8_t root[32];               /* Ed25519 public key */
    uint32_t product;
    uint32_t min_revision;
    uint32_t min_serial;
} sealed_board;

/* A firmware update package's description (doc/admin/firmware-updates.md
   section 5), from block 0 */
enum { SEALED_V5F = 1, SEALED_STAGE1 = 2,
       SEALED_V5F_APP = 3 };            /* the V5F slot, padded, then LiteForth's flash pages */
typedef struct {
    uint32_t component;             /* SEALED_V5F, SEALED_STAGE1 or SEALED_V5F_APP */
    uint32_t hw_rev;                /* 0: any */
    uint32_t length;                /* the image's bytes */
    uint32_t min_version;           /* the new minimum, or 0 */
    uint32_t version;               /* the revision field: the image's version */
    uint8_t image_hash[32];         /* BLAKE2s(image, 32, person "LFimage") */
    /* The machine-mode region's root signature (doc/admin/firmware-updates.md
       section 8, part 4; encrypted-blocks.md section 3). mmode_addr 0 means
       the package carries none; otherwise the root key (board->root) has
       signed these over the region, which sealed_check_signed verifies, and
       the caller hashes that flash span and compares it with mmode_hash. */
    uint32_t mmode_addr;            /* machine-mode region start, or 0: none */
    uint32_t mmode_len;             /* its length */
    uint8_t mmode_hash[32];         /* BLAKE2s(region, 32, person "LFmmode") */
} sealed_package;

/* Reads raw block blk (4096 bytes) of the file into buf, bypassing
   decryption: for the hash table blocks. buf is 4-byte aligned, so a
   target can read into it by DMA. Returns 0 or an error code. */
typedef int (*sealed_read_fn)(uint32_t blk, uint8_t *buf);

/**
 * Checks block 0 of a firmware update package (PACKAGE_MAGIC) of
 * file_blocks blocks as a board does: its fields and description (which
 * fill pkg), the release key's certificate (with board->root), the release
 * key's serial, block 0's signature, the version (board->min_revision is
 * the lowest version the board takes for this kind of image), then finds
 * board->uid among the recipients and unwraps the content key. `read`
 * will read the hash table blocks later. Unmounts any package mounted
 * before. msg receives one line saying what was mounted or why not.
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
 * sealed_check_package without the board's private key, for a core that
 * doesn't hold it (the CH32H417's V5F, for `-2 ymodem`): every check up
 * to finding board->uid among the recipients, then every content block
 * against its hash, but no unwrapping and so no image hash. A package for
 * this board's old key (enrolled again since) passes here; stage 1
 * refuses it. Unmounts it afterwards.
 * Returns 0, ERR_BLK_PARSE_FAIL, ERR_BLOCK_READ_ERROR, or read's error.
 */
int sealed_verify_package(uint32_t file_blocks, const sealed_board *board,
                          sealed_read_fn read, uint8_t *buf, sealed_package *pkg,
                          char *msg, int len);

/**
 * Checks a package's block 0 on its own, as stage 1 does at every boot
 * with the copy it keeps of the installed image's (src/update.c): the
 * fields, the release key's certificate (board->root), its serial
 * (board->min_serial), the signature and the version (board->min_revision),
 * and fills pkg, whose image_hash the caller compares with the image. No
 * recipients, no key, nothing mounted (unmounts any package mounted).
 * Returns 0 or ERR_BLK_PARSE_FAIL, msg saying why.
 */
int sealed_check_signed(const uint8_t *b0, const sealed_board *board,
                        sealed_package *pkg, char *msg, int len);

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
 * Checks block n, just read from the file into buf, against its hash and
 * decrypts it in place. Block 0 is left as it is. Reads the hash table
 * block that covers n (with the `read` given to sealed_mount_package) and checks
 * it against block 0, unless it's the one it read last.
 * Returns 0, ERR_BLOCK_READ_ERROR if a hash doesn't match or a crypto
 * engine failed partway (buf is then zeroed), ERR_BLK_BOUNDS if n isn't a content block, or read's error.
 */
int sealed_open_block(uint32_t n, uint8_t *buf);

#endif /* SEALED_H */
