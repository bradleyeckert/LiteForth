/* The data logs (src/datalog.c, src/logfile.c, exfat_create/exfat_trim)
 * on card images, for test_datalog.py:
 *
 *   dltest write IMG NAME BYTES RECORDS SEED SERVER_PUB BOARD_KEY PLAIN [CUT]
 *       makes the log file NAME (BYTES long) and writes a log of RECORDS
 *       records of made-up sample blocks (SEED), closing it; the plaintext
 *       sample blocks go to PLAIN. SERVER_PUB and BOARD_KEY are 32-byte raw
 *       key files. With CUT, the power goes at the CUT-th sector write
 *       (counting from 0): the program exits with 3, that write not done.
 *       RECORDS 0 with CUT never closes.
 *   dltest open IMG NAME BYTES RECORDS SEED SERVER_PUB BOARD_KEY PLAIN
 *       writes the log as write does, but doesn't close it.
 *   dltest recover IMG BYTES
 *       closes the logs a power cut left open; prints "NAME BLOCKS ERR".
 *   dltest cat IMG NAME OUT
 *       copies the file's ValidDataLength bytes to OUT.
 *   dltest auth BOARD_KEY E S
 *       prints dl_board_auth's k_auth in hex, for the Python side.
 *   dltest engine        (built with -DCTR_HW as dltest_hw)
 *       an engine (ctr_hw_crypt) that fails partway: dl_record and dl_open
 *       must give up, wipe the buffer, and dl_record reuse the number.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datalog.h"
#include "logfile.h"
#include "exfat.h"
#include "errcodes.h"
#include "monocypher.h"

static FILE *img;
static long writes, cut_at = -1;

static int img_read(uint32_t sector, uint8_t *buf) {
    if (fseek(img, (long)sector * 512, SEEK_SET) || fread(buf, 1, 512, img) != 512) {
        return ERR_BLK_READ_FAIL;
    }
    return 0;
}

static int img_write(uint32_t sector, const uint8_t *buf) {
    if (writes++ == cut_at) {
        fflush(img);
        exit(3);
    }
    if (fseek(img, (long)sector * 512, SEEK_SET) || fwrite(buf, 1, 512, img) != 512) {
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}

static void load(const char *path, uint8_t *out, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f || fread(out, 1, n, f) != n) {
        fprintf(stderr, "dltest: can't read %s\n", path);
        exit(2);
    }
    fclose(f);
}

static uint8_t board_key[32];

static int auth(const uint8_t E[32], const uint8_t S[32], uint8_t k[32]) {
    return dl_board_auth(board_key, E, S, k);
}

static uint32_t rng;
static uint8_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return (uint8_t)(rng >> 16);
}

static exfat_vol vol;

static void mount(const char *path) {
    img = fopen(path, "r+b");
    if (!img || exfat_mount(img_read, &vol) != 0) {
        fprintf(stderr, "dltest: can't mount %s\n", path);
        exit(2);
    }
}

#define TS 0x5B561234u                  /* some time in 2025 */

static uint8_t rec[DL_RECORD];

static int write_log(int argc, char **argv, int close) {
    if (argc < 10) {
        return 2;
    }
    mount(argv[2]);
    const char *name = argv[3];
    uint64_t bytes = strtoull(argv[4], NULL, 0);
    uint32_t records = (uint32_t)strtoul(argv[5], NULL, 0);
    rng = (uint32_t)strtoul(argv[6], NULL, 0);
    uint8_t S[32], e[32], file_id[8], app[DL_APP_HEADER];
    load(argv[7], S, 32);
    load(argv[8], board_key, 32);
    FILE *plain = fopen(argv[9], "wb");
    if (argc > 10) {
        cut_at = strtol(argv[10], NULL, 0);
    }
    exfat_file f;
    int err = log_make(img_read, img_write, &vol, name, bytes, TS, &f);
    if (err) {
        fprintf(stderr, "dltest: log_make: %d\n", err);
        return 1;
    }
    for (int i = 0; i < 32; i++) e[i] = rnd();
    for (int i = 0; i < 8; i++) file_id[i] = rnd();
    for (unsigned i = 0; i < DL_APP_HEADER; i++) app[i] = (uint8_t)(i * 7);
    dl_identity id = { { 1, 2, 3, 4, 5, 6, 7, 8 }, 1761100000ull, 42, 2 };
    dl_state d;
    if (dl_open(&d, S, 7, e, file_id, &id, app, auth, rec) != 0) {
        fprintf(stderr, "dltest: dl_open failed\n");
        return 1;
    }
    for (uint32_t s = 0; s < DL_RECORD / 512; s++) {
        if ((err = img_write(f.first_sector + s, rec + s * 512)) != 0) return 1;
    }
    uint32_t t = 1000;
    for (uint32_t r = 1; r <= records; r++) {
        uint32_t samples = (r == records) ? 100 : DL_SAMPLES;   /* the last one short */
        for (uint32_t k = 0; k < samples; k++) {
            uint8_t *b = rec + DL_PAYLOAD + k * DL_SAMPLE;
            b[0] = (uint8_t)(rnd() & 1);                        /* CODEC 0 or 1 */
            b[1] = b[2] = b[3] = 0;
            t += 133 + (rnd() & 7);
            for (int i = 0; i < 4; i++) b[4 + i] = (uint8_t)(t >> (8 * i));
            for (int i = 8; i < (int)DL_SAMPLE; i++) b[i] = rnd();
        }
        fwrite(rec + DL_PAYLOAD, DL_SAMPLE, samples, plain);
        fflush(plain);
        uint32_t n = dl_record(&d, rec, samples, close && r == records);
        if (n != r) {
            fprintf(stderr, "dltest: record %u came out as %u\n", r, n);
            return 1;
        }
        uint32_t at = f.first_sector + r * (DL_RECORD / 512);
        for (uint32_t s = 0; s < DL_RECORD / 512; s++) {
            if ((err = img_write(at + s, rec + s * 512)) != 0) return 1;
        }
    }
    dl_close(&d);
    fclose(plain);
    if (close) {
        err = log_close(img_read, img_write, &vol, &f, records + 1, TS + 60);
        if (err) {
            fprintf(stderr, "dltest: log_close: %d\n", err);
            return 1;
        }
    }
    fclose(img);
    printf("%ld writes\n", writes);
    return 0;
}

static void found(void *ctx, const char *name, uint32_t blocks, int err) {
    (void)ctx;
    printf("%s %u %d\n", name, blocks, err);
}

static uint8_t hexval(char c) {
    return (uint8_t)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
}

static void unhex(const char *s, uint8_t *out) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1]));
}

#ifdef CTR_HW
/* A stand-in for the CH32H417's ECDC engine: software, but it can be made
   to fail partway, leaving the buffer half done, as ecdc.c reports with 1 */
static int engine_fail_at = -1, engine_calls;

int ctr_hw_crypt(const ctr_key *k, const uint8_t counter[16], uint8_t *buf, size_t len) {
    if (engine_calls++ == engine_fail_at) {
        for (size_t i = 0; i < len / 2; i++) buf[i] ^= 0x5A;
        return 1;
    }
    uint64_t c = 0;
    for (int i = 8; i < 16; i++) c = c << 8 | counter[i];
    for (size_t at = 0; at < len; at += 16, c++) {
        uint8_t blk[16], ks[16];
        memcpy(blk, counter, 8);
        for (int i = 0; i < 8; i++) blk[15 - i] = (uint8_t)(c >> (8 * i));
        ctr_encrypt_block(k, blk, ks);
        for (int i = 0; i < 16 && at + i < len; i++) buf[at + i] ^= ks[i];
    }
    return 0;
}

static int engine_test(void) {
    static uint8_t a[DL_RECORD], b[DL_RECORD];
    uint8_t S[32], e[32], fid[8] = { 9 };
    dl_identity id = { { 1 }, 0, 0, 0 };
    dl_state d;
    int bad = 0;
    for (int i = 0; i < 32; i++) { S[i] = (uint8_t)(i + 9); board_key[i] = (uint8_t)(3 * i); }
    crypto_x25519_public_key(S, S);
    for (int i = 0; i < 32; i++) e[i] = (uint8_t)(i * 5);
    engine_calls = 0;
    engine_fail_at = 0;                     /* the application header */
    if (dl_open(&d, S, 1, e, fid, &id, NULL, auth, a) == 0) { puts("dl_open went on"); bad++; }
    for (unsigned i = 0; i < DL_RECORD; i++) if (a[i]) { puts("block 0 not wiped"); bad++; break; }
    for (int i = 0; i < 32; i++) e[i] = (uint8_t)(i * 5);
    engine_fail_at = -1;
    if (dl_open(&d, S, 1, e, fid, &id, NULL, auth, a) != 0) { puts("dl_open failed"); bad++; }
    memset(a + DL_PAYLOAD, 0x77, DL_SAMPLES * DL_SAMPLE);
    engine_calls = 0;
    engine_fail_at = 3;                     /* record 1's fourth 4 KB piece */
    if (dl_record(&d, a, DL_SAMPLES, 0) != 0) { puts("dl_record went on"); bad++; }
    for (unsigned i = 0; i < DL_RECORD; i++) if (a[i]) { puts("record not wiped"); bad++; break; }
    memset(a + DL_PAYLOAD, 0x77, DL_SAMPLES * DL_SAMPLE);
    memcpy(b, a, DL_RECORD);
    engine_fail_at = -1;
    uint32_t n = dl_record(&d, a, DL_SAMPLES, 0);
    if (n != 1) { printf("the retry made record %u, not 1\n", n); bad++; }
    for (unsigned i = DL_PAYLOAD; i < DL_PAYLOAD + DL_SAMPLES * DL_SAMPLE; i++)
        if (a[i] == b[i] && a[i + 1] == b[i + 1] && a[i + 2] == b[i + 2] && a[i + 3] == b[i + 3]) {
            puts("plaintext left in the retried record"); bad++; break;
        }
    dl_close(&d);
    puts(bad ? "engine: FAILED" : "engine: ok");
    return bad != 0;
}
#endif

int main(int argc, char **argv) {
#ifdef CTR_HW
    if (argc == 2 && strcmp(argv[1], "engine") == 0) {
        return engine_test();
    }
#endif
    if (argc >= 10 && strcmp(argv[1], "write") == 0) {
        return write_log(argc, argv, 1);
    }
    if (argc >= 10 && strcmp(argv[1], "open") == 0) {
        return write_log(argc, argv, 0);
    }
    if (argc == 4 && strcmp(argv[1], "recover") == 0) {
        mount(argv[2]);
        int n = log_recover(img_read, img_write, &vol, strtoull(argv[3], NULL, 0), TS + 120,
                            found, NULL);
        printf("closed %d\n", n);
        fclose(img);
        return n < 0;
    }
    if (argc == 5 && strcmp(argv[1], "cat") == 0) {
        mount(argv[2]);
        exfat_file f;
        if (exfat_find(img_read, &vol, vol.root, argv[3], &f) != 0) {
            fprintf(stderr, "dltest: no %s\n", argv[3]);
            return 1;
        }
        FILE *out = fopen(argv[4], "wb");
        uint8_t buf[512];
        for (uint64_t at = 0; at < f.valid; at += 512) {
            img_read(f.first_sector + (uint32_t)(at / 512), buf);
            fwrite(buf, 1, f.valid - at < 512 ? (size_t)(f.valid - at) : 512, out);
        }
        fclose(out);
        printf("%llu %llu\n", (unsigned long long)f.length, (unsigned long long)f.valid);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "auth") == 0) {
        uint8_t E[32], S[32], k[32];
        load(argv[2], board_key, 32);
        unhex(argv[3], E);
        unhex(argv[4], S);
        if (dl_board_auth(board_key, E, S, k)) return 1;
        for (int i = 0; i < 32; i++) printf("%02x", k[i]);
        printf("\n");
        return 0;
    }
    fprintf(stderr, "usage: see dltest.c\n");
    return 2;
}
