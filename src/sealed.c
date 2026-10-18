/* Sealed block files: mounting and reading (doc/encrypted-blocks.md).
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

static uint8_t header[SEALED_BLOCK_BYTES];      /* the mounted block 0 */
static uint32_t table_cells[SEALED_BLOCK_BYTES / 4];   /* cells: a DMA target */
#define table ((uint8_t *)table_cells)          /* the last table block read */
static uint32_t table_index = UINT32_MAX;       /* which one, or none */
static ctr_key key;
static uint8_t nonce[8];
static uint32_t content_end;                    /* C, or 0: nothing mounted */
static uint32_t tables;                         /* T */
static uint32_t revision;
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
    content_end = tables = revision = 0;
    table_index = UINT32_MAX;
    read_raw = NULL;
}

uint32_t sealed_blocks(void) {
    return content_end;
}

uint32_t sealed_revision(void) {
    return revision;
}

/* sealed_mount, or with pkg a package (sealed_mount_package) */
static int mount(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                 sealed_read_fn read, char *msg, int len, sealed_package *pkg) {
    sealed_unmount();
    memcpy(header, b0, SEALED_BLOCK_BYTES);
    const uint8_t *h = header;
    uint32_t end = pkg ? IMAGE_HASH_AT : SIG_AT;

    /* the fields */
    if (memcmp(h, pkg ? PACKAGE_MAGIC : SEALED_MAGIC, SEALED_MAGIC_LEN) != 0) {
        return fail(msg, len, pkg ? "block 0 doesn't start with " PACKAGE_MAGIC
                                  : "block 0 doesn't start with " SEALED_MAGIC);
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
    if (pkg) {                                  /* the image's description */
        pkg->component = h[180];
        pkg->hw_rev = get16(h + 182);
        pkg->length = get32(h + 184);
        pkg->min_version = get32(h + 188);
        pkg->version = rev;
        memcpy(pkg->image_hash, h + IMAGE_HASH_AT, 32);
        if (pkg->component != SEALED_V5F && pkg->component != SEALED_STAGE1) {
            return fail(msg, len, "unknown component");
        }
        if (h[181]) {
            return fail(msg, len, "unknown image flags");
        }
        if (pkg->length <= (c - 2) * SEALED_BLOCK_BYTES
            || pkg->length > (c - 1) * SEALED_BLOCK_BYTES) {
            return fail(msg, len, "the image's length doesn't match its blocks");
        }
    } else {
        for (int i = 180; i < TABLE_HASHES; i++) {
            if (h[i]) {
                return fail(msg, len, "data in block 0's reserved bytes");
            }
        }
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
    uint8_t shared[32], board_pub[32], kek[16], cek[16], check[8], salt[8];
    static const uint8_t zero[32];
    crypto_x25519(shared, board->private_key, h + 40);
    if (crypto_verify32(shared, zero) == 0) {
        crypto_wipe(shared, sizeof shared);
        return fail(msg, len, "bad ephemeral key");
    }
    crypto_x25519_public_key(board_pub, board->private_key);
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

    memcpy(nonce, h + 32, 8);
    content_end = c;
    tables = t;
    revision = rev;
    read_raw = read;
    snprintf(msg, len, "%s, revision %lu, release key %lu, %s, %lu blocks",
             pkg ? "package" : "sealed file", (unsigned long)rev,
             (unsigned long)get32(h + 80), h[11] == CTR_SM4 ? "SM4" : "AES-128",
             (unsigned long)c);
    return 0;
}

int sealed_mount(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                 sealed_read_fn read, char *msg, int len) {
    return mount(b0, file_blocks, board, read, msg, len, NULL);
}

int sealed_mount_package(const uint8_t *b0, uint32_t file_blocks, const sealed_board *board,
                         sealed_read_fn read, char *msg, int len, sealed_package *pkg) {
    return mount(b0, file_blocks, board, read, msg, len, pkg);
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
    if (ctr_crypt_block(&key, nonce, n, buf, SEALED_BLOCK_BYTES) != 0) {
        memset(buf, 0, SEALED_BLOCK_BYTES);     /* the engine failed partway */
        return ERR_BLOCK_READ_ERROR;
    }
    return 0;
}
