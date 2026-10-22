/* Encrypted, authenticated data logs (datalog.h, doc/admin/data-logs.md) */
#include <string.h>
#include "datalog.h"
#include "blake2s.h"
#include "monocypher.h"

/* Block 0 */
#define H_MAGIC     0
#define H_VERSION   8           /* format 1, cipher 1, flags 0, 0 */
#define H_KEY_ID    12
#define H_FILE_ID   16
#define H_E         24
#define H_IDENTITY  56          /* 32 bytes, XOR k_idx */
#define H_APP       128         /* DL_APP_HEADER bytes, CTR under k_enc, block 0 */
#define H_TAG       (H_APP + DL_APP_HEADER)    /* 16: BLAKE2s(0..H_TAG-1, k_hdr) */

/* A record */
#define R_MAGIC     0           /* "LFLB" */
#define R_FILE_ID   4
#define R_N         12
#define R_SAMPLES   16
#define R_FLAGS     18
#define R_TAG       (DL_PAYLOAD + DL_SAMPLES * DL_SAMPLE)  /* 32704 */
#define R_END_N     (DL_RECORD - 16)                     /* 32752 */
#define R_END_ID    (DL_RECORD - 12)
#define R_END_MAGIC (DL_RECORD - 4)                      /* "LFLE" */
#define CTR_PER_RECORD  (DL_RECORD / 4096u)              /* ctr.c blocks per record: 8 */

static void put32(uint8_t *p, uint32_t x) {
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(x >> (8 * i));
    }
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* BLAKE2s, digest `size`, of a || b, keyed, personalized */
static void kdf(uint8_t *out, int size, const uint8_t *key, int klen, const char *person,
                const uint8_t *a, int alen, const uint8_t *b, int blen) {
    blake2s_state S;
    uint8_t p[8] = {0};
    memcpy(p, person, strlen(person) < 8 ? strlen(person) : 8);
    b2s_init(&S, size, key, klen, NULL, p);
    if (alen) {
        b2s_puts(&S, a, alen);
    }
    if (blen) {
        b2s_puts(&S, b, blen);
    }
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
}

int dl_board_auth(const uint8_t b[32], const uint8_t E[32], const uint8_t S[32],
                  uint8_t k_auth[32]) {
    static const uint8_t zero[32];
    uint8_t ss[32];
    crypto_x25519(ss, b, S);
    int bad = crypto_verify32(ss, zero) == 0;
    if (!bad) {
        kdf(k_auth, 32, ss, 32, "LFlogbrd", E, 32, S, 32);
    }
    crypto_wipe(ss, sizeof ss);
    return bad ? -1 : 0;
}

int dl_open(dl_state *d, const uint8_t S[32], uint32_t key_id, uint8_t e[32],
            const uint8_t file_id[8], const dl_identity *id, const uint8_t *app_header,
            dl_auth_fn auth, uint8_t *block0) {
    static const uint8_t zero[32];
    uint8_t E[32], es[32], k_id[32], k_auth[32], master[32], k[32], es_E_S[96];
    memset(d, 0, sizeof *d);
    memset(block0, 0, DL_RECORD);
    crypto_x25519_public_key(E, e);
    crypto_x25519(es, e, S);
    crypto_wipe(e, 32);
    int bad = crypto_verify32(es, zero) == 0 || auth(E, S, k_auth) != 0;
    if (bad) {
        crypto_wipe(es, sizeof es);
        crypto_wipe(k_auth, sizeof k_auth);
        return -1;
    }
    /* the keys (data-logs.md section 2) */
    kdf(k_id, 32, es, 32, "LFlogid", E, 32, S, 32);
    memcpy(es_E_S, es, 32);
    memcpy(es_E_S + 32, E, 32);
    memcpy(es_E_S + 64, S, 32);
    kdf(master, 32, k_auth, 32, "LFlogkey", es_E_S, 96, NULL, 0);
    kdf(k, 16, master, 32, "LFlogenc", NULL, 0, NULL, 0);
    ctr_setkey(&d->key, CTR_AES128, k);
    kdf(d->nonce, 8, master, 32, "LFlognon", NULL, 0, NULL, 0);
    memcpy(d->file_id, file_id, 8);
    d->n = 1;

    /* block 0 */
    memcpy(block0 + H_MAGIC, DL_MAGIC, 8);
    block0[H_VERSION] = 1;
    block0[H_VERSION + 1] = CTR_AES128;
    put32(block0 + H_KEY_ID, key_id);
    memcpy(block0 + H_FILE_ID, file_id, 8);
    memcpy(block0 + H_E, E, 32);
    uint8_t *ident = block0 + H_IDENTITY, pad[32];
    memcpy(ident, id->uid, 8);
    put32(ident + 8, (uint32_t)id->start_time);
    put32(ident + 12, (uint32_t)(id->start_time >> 32));
    put32(ident + 16, id->firmware);
    ident[20] = id->level;
    kdf(pad, 32, k_id, 32, "LFlogidx", NULL, 0, NULL, 0);
    for (int i = 0; i < 32; i++) {
        ident[i] ^= pad[i];
    }
    if (app_header) {
        memcpy(block0 + H_APP, app_header, DL_APP_HEADER);
    }
    ctr_crypt_block(&d->key, d->nonce, 0, block0 + H_APP, DL_APP_HEADER);
    kdf(k, 32, master, 32, "LFloghdr", NULL, 0, NULL, 0);
    kdf(block0 + H_TAG, 16, k, 32, "LFloghtg", block0, H_TAG, NULL, 0);

    crypto_wipe(es, sizeof es);
    crypto_wipe(es_E_S, sizeof es_E_S);
    crypto_wipe(k_id, sizeof k_id);
    crypto_wipe(k_auth, sizeof k_auth);
    crypto_wipe(master, sizeof master);
    crypto_wipe(k, sizeof k);
    crypto_wipe(pad, sizeof pad);
    return 0;
}

/* The keystream at counter nonce || c, 16 bytes */
static void keystream(const dl_state *d, uint64_t c, uint8_t out[16]) {
    uint8_t block[16];
    memcpy(block, d->nonce, 8);
    for (int i = 0; i < 8; i++) {
        block[15 - i] = (uint8_t)(c >> (8 * i));
    }
    ctr_encrypt_block(&d->key, block, out);
}

uint32_t dl_record(dl_state *d, uint8_t *rec, uint32_t samples, int last) {
    if (samples > DL_SAMPLES || d->n == 0 || d->n >= 0x1FFFFFFFu) {
        return 0;
    }
    uint32_t n = d->n++;
    memset(rec, 0, DL_PAYLOAD);
    memset(rec + DL_PAYLOAD + samples * DL_SAMPLE, 0, (DL_SAMPLES - samples) * DL_SAMPLE);
    memset(rec + R_TAG, 0, DL_RECORD - R_TAG);
    memcpy(rec + R_MAGIC, "LFLB", 4);
    memcpy(rec + R_FILE_ID, d->file_id, 8);
    put32(rec + R_N, n);
    rec[R_SAMPLES] = (uint8_t)samples;
    rec[R_FLAGS] = last ? DL_FLAG_LAST : 0;
    put32(rec + R_END_N, n);
    memcpy(rec + R_END_ID, d->file_id, 8);
    memcpy(rec + R_END_MAGIC, "LFLE", 4);

    /* the sample blocks: units n * 2048 + 0..2039, as ctr.c's 4 KB blocks
       n * 8 + j (each 256 units) */
    uint8_t *p = rec + DL_PAYLOAD;
    uint32_t left = DL_SAMPLES * DL_SAMPLE;
    for (uint32_t j = 0; left; j++) {
        uint32_t take = left < 4096u ? left : 4096u;
        ctr_crypt_block(&d->key, d->nonce, n * CTR_PER_RECORD + j, p, take);
        p += take;
        left -= take;
    }
    uint8_t pk[32];
    keystream(d, (uint64_t)n * 2048u + 2046u, pk);
    keystream(d, (uint64_t)n * 2048u + 2047u, pk + 16);
    crypto_poly1305(rec + R_TAG, rec, R_TAG, pk);
    crypto_wipe(pk, sizeof pk);
    return n;
}

void dl_close(dl_state *d) {
    crypto_wipe(d, sizeof *d);
}

/* Whether block k holds a record of the log file_id, by its frame */
static int written(dl_read_fn read, void *ctx, uint32_t k, const uint8_t id[8],
                   uint8_t *buf, int *err) {
    uint32_t spb = DL_RECORD / DL_SECTOR;
    *err = read(ctx, k * spb, buf);
    if (*err) {
        return 0;
    }
    if (memcmp(buf + R_MAGIC, "LFLB", 4) != 0 || memcmp(buf + R_FILE_ID, id, 8) != 0
        || get32(buf + R_N) != k) {
        return 0;
    }
    *err = read(ctx, k * spb + spb - 1, buf);
    if (*err) {
        return 0;
    }
    const uint8_t *t = buf + DL_SECTOR - 16;    /* R_END_N within the last sector */
    return get32(t) == k && memcmp(t + 4, id, 8) == 0 && memcmp(t + 12, "LFLE", 4) == 0;
}

int32_t dl_find_end(dl_read_fn read, void *ctx, uint32_t blocks, uint8_t *buf) {
    uint8_t id[8];
    int err = read(ctx, 0, buf);
    if (err) {
        return err < 0 ? err : -1;
    }
    if (memcmp(buf + H_MAGIC, DL_MAGIC, 8) != 0 || blocks == 0) {
        return 0;
    }
    memcpy(id, buf + H_FILE_ID, 8);
    /* blocks 1 .. lo-1 are written, hi and beyond aren't */
    uint32_t lo = 1, hi = blocks;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (written(read, ctx, mid, id, buf, &err)) {
            lo = mid + 1;
        } else if (err) {
            return err < 0 ? err : -1;
        } else {
            hi = mid;
        }
    }
    return (int32_t)lo;
}
