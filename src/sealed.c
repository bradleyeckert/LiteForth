/* The sealed format: mounting and reading firmware update packages
   (doc/admin/encrypted-blocks.md; sealed block files are retired).
   The layout constants below are the table in its section 3. */
#include <stdio.h>
#include <string.h>
#include "sealed.h"
#include "blake2s.h"
#include "ctr.h"
#include "errcodes.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

#define FORMAT_VERSION  1
#define PER_TABLE       256             /* block hashes per table block */
#define ENTRY           16              /* bytes per block hash */
#define TABLE_HASHES    192             /* offset of the table hashes */
#define RECIPIENT       24              /* UID (8) + wrapped CEK (16) */
#define SIG_AT          (SEALED_BLOCK_BYTES - 64)
#define IMAGE_HASH_AT   (SIG_AT - 32)   /* a package's plaintext image hash */

/* The mounted block 0, and the last table block read (cells: a DMA
   target). With SEALED_POOL (the CH32H417's V5F, lf_sealed.c) they come
   from memalloc.c's pool while a package is mounted, else they're static. */
#ifdef SEALED_POOL
#include "memalloc.h"
static uint32_t *work;                  /* both, from the pool */
static uint8_t *header;
static uint32_t *table_cells;

static int work_get(void) {
    if (work == NULL) {
        work = pool_alloc(2 * SEALED_BLOCK_BYTES / 4);
        if (work == NULL) {
            return -1;
        }
        header = (uint8_t *)work;
        table_cells = work + SEALED_BLOCK_BYTES / 4;
    }
    return 0;
}

/* Gives them back, if nothing newer was taken from the pool since (else
   they stay, to be used by the next mount) */
static void work_put(void) {
    if (work != NULL && pool_is_newest(work)) {
        crypto_wipe(work, 2 * SEALED_BLOCK_BYTES);
        pool_free(work);
        work = NULL;
        header = NULL;
        table_cells = NULL;
    }
}
#else
static uint8_t header[SEALED_BLOCK_BYTES];
static uint32_t table_cells[SEALED_BLOCK_BYTES / 4];
static int work_get(void) { return 0; }
static void work_put(void) { }
#endif
#define table ((uint8_t *)table_cells)
static uint32_t table_index = UINT32_MAX;       /* which one, or none */
static ctr_key key;
static uint8_t nonce[8];
static uint32_t content_end;                    /* C, or 0: nothing mounted */
static uint32_t tables;                         /* T */
static int keyless;                             /* mounted without the key: blocks
                                                   are checked, not decrypted */
static sealed_read_fn read_raw;

static uint32_t get16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return get16(p) | (get16(p + 2) << 16);
}

static void le64(uint8_t out[8], uint32_t x) {
    memset(out, 0, 8);
    for (int i = 0; i < 4; i++) {
        out[i] = (uint8_t)(x >> (8 * i));
    }
}

/* BLAKE2s of data with the given size, key, salt and personalization */
static void b2s(uint8_t *out, int size, const uint8_t *k, int klen,
                const uint8_t salt[8], const char *person, const uint8_t *data, int len) {
    blake2s_state S;
    uint8_t p[8] = {0};
    memcpy(p, person, strlen(person) < 8 ? strlen(person) : 8);
    b2s_init(&S, size, k, klen, salt, p);
    b2s_puts(&S, data, len);
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
}

static int fail(char *msg, int len, const char *why) {
    snprintf(msg, len, "sealed file refused: %s", why);
    sealed_unmount();
    return ERR_BLK_PARSE_FAIL;
}

void sealed_unmount(void) {
    ctr_wipe(&key);
    crypto_wipe(nonce, sizeof nonce);
    content_end = tables = 0;
    keyless = 0;
    table_index = UINT32_MAX;
    read_raw = NULL;
    work_put();
}

uint32_t sealed_blocks(void) {
    return content_end;
}

enum { KEYLESS, UNWRAP, SIGNED_ONLY };

/* sealed_mount_package (UNWRAP); KEYLESS, the same checks up to finding
   this board among the recipients, with no key (sealed_verify_package);
   SIGNED_ONLY, only up to the signature and version, mounting nothing
   (sealed_check_signed) */
static int mount(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                 sealed_read_fn read, char *msg, int len, sealed_package *pkg, int unwrap) {
    sealed_unmount();
    if (work_get() != 0) {
        snprintf(msg, len, "sealed file refused: no memory left to check it");
        return ERR_ALLOCATE_FAILED;
    }
    memcpy(header, b0, SEALED_BLOCK_BYTES);
    const uint8_t *h = header;
    uint32_t end = IMAGE_HASH_AT;

    /* the fields */
    if (memcmp(h, PACKAGE_MAGIC, SEALED_MAGIC_LEN) != 0) {
        return fail(msg, len, "block 0 doesn't start with " PACKAGE_MAGIC);
    }
    if (h[10] != FORMAT_VERSION) {
        return fail(msg, len, "unknown format version");
    }
    if (h[11] != CTR_AES128 && h[11] != CTR_SM4) {
        return fail(msg, len, "unknown cipher");
    }
    if (get32(h + 12) != 0) {
        return fail(msg, len, "unknown flags");
    }
    uint32_t product = get32(h + 16), rev = get32(h + 20), c = get32(h + 24);
    uint32_t t = get16(h + 28), r = get16(h + 30);
    if (product != board->product) {
        return fail(msg, len, "made for another product");
    }
    if (c < 2 || t != (c + PER_TABLE - 1) / PER_TABLE) {
        return fail(msg, len, "bad block counts");
    }
    if (r == 0 || TABLE_HASHES + 32 * t + RECIPIENT * r > end) {
        return fail(msg, len, "the recipients don't fit block 0");
    }
    if (c + t < c || c + t > file_blocks) {
        return fail(msg, len, "the file is shorter than its header says");
    }
    for (uint32_t i = TABLE_HASHES + 32 * t + RECIPIENT * r; i < end; i++) {
        if (h[i]) {
            return fail(msg, len, "data in block 0's reserved bytes");
        }
    }
    pkg->component = h[180];                    /* the image's description */
    pkg->hw_rev = get16(h + 182);
    pkg->length = get32(h + 184);
    pkg->min_version = get32(h + 188);
    pkg->version = rev;
    memcpy(pkg->image_hash, h + IMAGE_HASH_AT, 32);
    if (pkg->component != SEALED_V5F && pkg->component != SEALED_STAGE1
        && pkg->component != SEALED_V5F_APP) {
        return fail(msg, len, "unknown component");
    }
    if (h[181]) {
        return fail(msg, len, "unknown image flags");
    }
    if (pkg->length <= (c - 2) * SEALED_BLOCK_BYTES
        || pkg->length > (c - 1) * SEALED_BLOCK_BYTES) {
        return fail(msg, len, "the image's length doesn't match its blocks");
    }

    /* the certificate: the root signed product ID, serial, release key */
    uint8_t cert[40];
    memcpy(cert, h + 16, 4);
    memcpy(cert + 4, h + 80, 36);
    if (crypto_ed25519_check(h + 116, board->root, cert, sizeof cert) != 0) {
        return fail(msg, len, "the release key's certificate doesn't match the root key");
    }
    if (get32(h + 80) < board->min_serial) {
        return fail(msg, len, "the release key has been replaced");
    }
    if (crypto_ed25519_check(h + SIG_AT, h + 84, h, SIG_AT) != 0) {
        return fail(msg, len, "block 0's signature doesn't match the release key");
    }
    if (rev < board->min_revision) {
        return fail(msg, len, "an older revision than this board takes");
    }
    if (unwrap == SIGNED_ONLY) {
        sealed_unmount();
        snprintf(msg, len, "revision %lu, release key %lu: signed",
                 (unsigned long)rev, (unsigned long)get32(h + 80));
        return 0;
    }

    /* this board's content key */
    const uint8_t *entry = NULL;
    for (uint32_t i = 0; i < r; i++) {
        const uint8_t *e = h + TABLE_HASHES + 32 * t + RECIPIENT * i;
        if (memcmp(e, board->uid, 8) == 0) {
            entry = e;
            break;
        }
    }
    if (entry == NULL) {
        return fail(msg, len, "this board isn't one of its recipients");
    }
    if (unwrap == KEYLESS) {
        keyless = 1;
        goto mounted;
    }
    uint8_t shared[32], board_pub[32], kek[16], cek[16], check[8], salt[8];
    static const uint8_t zero[32];
    crypto_x25519(shared, board->private_key, h + 40);
    crypto_x25519_public_key(board_pub, board->private_key);
    if (crypto_verify32(shared, zero) == 0) {
        crypto_wipe(shared, sizeof shared);
        return fail(msg, len, "bad ephemeral key");
    }
    uint8_t pubs[64];
    memcpy(pubs, h + 40, 32);
    memcpy(pubs + 32, board_pub, 32);
    memcpy(salt, h + 20, 4);                    /* revision, then product ID */
    memcpy(salt + 4, h + 16, 4);
    b2s(kek, 16, shared, 32, salt, "LFwrap", pubs, 64);
    for (int i = 0; i < 16; i++) {
        cek[i] = entry[8 + i] ^ kek[i];
    }
    b2s(check, 8, cek, 16, NULL, "LFcheck", h + 32, 8);
    int ok = memcmp(check, h + 72, 8) == 0;
    if (ok) {
        ctr_setkey(&key, h[11], cek);
    }
    crypto_wipe(shared, sizeof shared);
    crypto_wipe(kek, sizeof kek);
    crypto_wipe(cek, sizeof cek);
    if (!ok) {
        return fail(msg, len, "the content key doesn't unwrap (CEK check)");
    }

mounted:
    memcpy(nonce, h + 32, 8);
    content_end = c;
    tables = t;
    read_raw = read;
    snprintf(msg, len, "package, revision %lu, release key %lu, %s, %lu blocks",
             (unsigned long)rev,
             (unsigned long)get32(h + 80), h[11] == CTR_SM4 ? "SM4" : "AES-128",
             (unsigned long)c);
    return 0;
}

int sealed_mount_package(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, char *msg, int len, sealed_package *pkg) {
    return mount(b0, file_blocks, board, read, msg, len, pkg, UNWRAP);
}

int sealed_check_signed(const uint8_t *b0, const sealed_board *board,
                        sealed_package *pkg, char *msg, int len) {
    return mount(b0, UINT32_MAX, board, NULL, msg, len, pkg, SIGNED_ONLY);
}

static const char *component_name(uint32_t c) {
    return c == SEALED_V5F ? "V5F" : c == SEALED_V5F_APP ? "V5F and app" : "stage 1";
}

/* sealed_check_package, or without the key sealed_verify_package */
static int check_package(uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, uint8_t *buf, sealed_package *pkg,
                         char *msg, int len, int unwrap) {
    sealed_board b = *board;
    b.min_revision = 0;                 /* stage 1 checks its own minimum */
    int err = read(0, buf);
    if (!err) {
        err = mount(buf, file_blocks, &b, read, msg, len, pkg, unwrap);
    }
    crypto_wipe(&b, sizeof b);
    if (err) {
        sealed_unmount();
        return err;
    }
    blake2s_state S;
    uint8_t p[8] = { 'L', 'F', 'i', 'm', 'a', 'g', 'e', 0 }, h[32];
    b2s_init(&S, 32, NULL, 0, NULL, p);
    for (uint32_t n = 1; n < content_end && !err; n++) {
        err = read(n, buf);
        if (!err) {
            err = sealed_open_block(n, buf);
        }
        if (err) {
            snprintf(msg, len, "block %lu of the package fails its hash", (unsigned long)n);
            break;
        }
        if (!unwrap) {
            continue;                   /* still encrypted: nothing to hash */
        }
        uint32_t at = (n - 1) * SEALED_BLOCK_BYTES;
        uint32_t take = pkg->length - at < SEALED_BLOCK_BYTES ? pkg->length - at
                                                              : SEALED_BLOCK_BYTES;
        b2s_puts(&S, buf, (int)take);
    }
    b2s_final(&S, h);
    sealed_unmount();
    if (!err && unwrap && memcmp(h, pkg->image_hash, 32) != 0) {
        snprintf(msg, len, "the image doesn't match its hash");
        err = ERR_BLOCK_READ_ERROR;
    }
    if (!err) {
        snprintf(msg, len, unwrap
                 ? "%s image, %lu bytes, version %lu: signature, every block "
                   "and the image hash check out"
                 : "%s image, %lu bytes, version %lu: signature, recipient and "
                   "blocks check out",          /* stage 1 checks the rest */
                 component_name(pkg->component),
                 (unsigned long)pkg->length, (unsigned long)pkg->version);
    }
    return err;
}

int sealed_check_package(uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, uint8_t *buf, sealed_package *pkg,
                         char *msg, int len) {
    return check_package(file_blocks, board, read, buf, pkg, msg, len, UNWRAP);
}

int sealed_verify_package(uint32_t file_blocks, const sealed_board *board,
                          sealed_read_fn read, uint8_t *buf, sealed_package *pkg,
                          char *msg, int len) {
    return check_package(file_blocks, board, read, buf, pkg, msg, len, KEYLESS);
}

int sealed_open_block(uint32_t n, uint8_t *buf) {
    if (n == 0) {
        return 0;
    }
    if (n >= content_end) {
        return ERR_BLK_BOUNDS;
    }
    uint32_t t = n / PER_TABLE;
    uint8_t salt[8], hash[32];
    if (t != table_index) {                     /* read and check the table */
        table_index = UINT32_MAX;
        int err = read_raw(content_end + t, table);
        if (err) {
            return err;
        }
        le64(salt, t);
        b2s(hash, 32, NULL, 0, salt, "LFtable", table, SEALED_BLOCK_BYTES);
        if (crypto_verify32(hash, header + TABLE_HASHES + 32 * t) != 0) {
            memset(buf, 0, SEALED_BLOCK_BYTES);
            return ERR_BLOCK_READ_ERROR;
        }
        table_index = t;
    }
    le64(salt, n);
    b2s(hash, 16, NULL, 0, salt, "LFblock", buf, SEALED_BLOCK_BYTES);
    if (crypto_verify16(hash, table + ENTRY * (n % PER_TABLE)) != 0) {
        memset(buf, 0, SEALED_BLOCK_BYTES);
        return ERR_BLOCK_READ_ERROR;
    }
    if (keyless) {
        return 0;                               /* checked; it stays encrypted */
    }
    if (ctr_crypt_block(&key, nonce, n, buf, SEALED_BLOCK_BYTES) != 0) {
        memset(buf, 0, SEALED_BLOCK_BYTES);     /* the engine failed partway */
        return ERR_BLOCK_READ_ERROR;
    }
    return 0;
}
