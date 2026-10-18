/*
 * The board's record for sealed block files (board.h): an 8 KB flash
 * sector after LiteForth's 256 KB of flash pages, holding one 256-byte
 * record:
 *
 *   0   magic "LFK1"
 *   4   lowest revision this board takes
 *   8   lowest release key serial
 *   12  reserved, 0
 *   16  X25519 private key (32 bytes)
 *   48  BLAKE2s-128 of bytes 0-47 (personalization "LFrecord")
 *   64  zeros to 256
 *
 * Erased flash reads 0xE339E339 on this chip (flash.h), so a record is
 * trusted only with the right magic and check. The key comes from the
 * RNG: 64 words, each checked (data ready, no seed or clock error, not a
 * repeat of the one before), hashed together with BLAKE2s.
 *
 * Security holes left for later (encrypted-blocks.md section 9): any code
 * on the V5F can read this sector, and nothing stops it being rewritten.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "board.h"
#include "flash.h"
#include "../../../../blake2s.h"
#include "../../../../monocypher.h"
#include "../../../../errcodes.h"
#include "../../../../tools.h"
#if __has_include("sealed_root.h")
#include "sealed_root.h"            /* SEALED_ROOT_KEY, SEALED_PRODUCT */
#endif

extern const uint32_t __lf_flash_start[];   /* Link_v5f.ld */
extern const uint8_t  __lf_flash_size[];

#define RECORD_ADDR     ((uint32_t)__lf_flash_start + (uint32_t)__lf_flash_size)
#define SECTOR_BYTES    8192u
#define RECORD_BYTES    256u
#define RECORD_MAGIC    0x314B464Cu         /* "LFK1" */
#define UID_ADDR        0x1FFFF7E8u         /* ESIG UNIID1, then UNIID2 */
#define RNG_WORDS       64
#define RNG_WAIT        100000              /* polls for a word before giving up */

typedef struct {
    uint32_t magic;
    uint32_t min_revision;
    uint32_t min_serial;
    uint32_t reserved;
    uint8_t private_key[32];
    uint8_t check[16];
    uint8_t zero[RECORD_BYTES - 64];
} record_t;

_Static_assert(sizeof(record_t) == RECORD_BYTES, "the record is one 256-byte program page");

static void record_check(const record_t *r, uint8_t out[16]) {
    blake2s_state S;
    static const uint8_t person[8] = "LFrecord";
    b2s_init(&S, 16, NULL, 0, NULL, person);
    b2s_puts(&S, (const uint8_t *)r, 48);
    b2s_final(&S, out);
}

/* Copies the record from flash; returns 1 if it's valid */
static int record_read(record_t *r) {
    uint8_t check[16];
    memcpy(r, (const void *)RECORD_ADDR, sizeof *r);
    record_check(r, check);
    return r->magic == RECORD_MAGIC && memcmp(check, r->check, 16) == 0;
}

static int record_write(record_t *r) {
    record_check(r, r->check);
    if (FLASH_ROM_ERASE(RECORD_ADDR, SECTOR_BYTES) != FLASH_COMPLETE
        || FLASH_ROM_WRITE(RECORD_ADDR, (uint32_t *)r, RECORD_BYTES) != FLASH_COMPLETE
        || memcmp((const void *)RECORD_ADDR, r, RECORD_BYTES) != 0) {
        return ERR_FLASH_WRITE_SECTOR;
    }
    return 0;
}

static void uid(uint8_t out[8]) {
    memcpy(out, (const void *)UID_ADDR, 8);
}

/* A private key from the RNG; returns 0 or -1 if the RNG misbehaves */
static int random_key(uint8_t key[32]) {
    static const uint8_t person[8] = "LFkeygen";
    blake2s_state S;
    uint32_t last = 0;
    RCC_HBPeriphClockCmd(RCC_HBPeriph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
    b2s_init(&S, 32, NULL, 0, NULL, person);
    for (int i = 0; i < RNG_WORDS; i++) {
        int wait = RNG_WAIT;
        while (RNG_GetFlagStatus(RNG_FLAG_DRDY) == RESET && --wait) {
        }
        if (!wait || RNG_GetFlagStatus(RNG_FLAG_SECS) || RNG_GetFlagStatus(RNG_FLAG_CECS)) {
            return -1;
        }
        uint32_t x = RNG_GetRandomNumber();
        if (i && x == last) {
            return -1;                      /* stuck */
        }
        last = x;
        b2s_puts(&S, (const uint8_t *)&x, 4);
    }
    b2s_final(&S, key);
    crypto_wipe(&S, sizeof S);
    return 0;
}

static void put_hex(const uint8_t *p, int n) {
    static const char digits[] = "0123456789abcdef";
    char s[3] = {0};
    for (int i = 0; i < n; i++) {
        s[0] = digits[p[i] >> 4];
        s[1] = digits[p[i] & 15];
        lf_puts(s);
    }
}

int board_get(sealed_board *b, char *msg, int len) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    snprintf(msg, len, "no root key built in (V5F/User/sealed_root.h)");
    return ERR_UNSUPPORTED_OPERATION;
#else
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    record_t r;
    if (!record_read(&r)) {
        snprintf(msg, len, "this board has no key: run enroll");
        return ERR_UNSUPPORTED_OPERATION;
    }
    uid(b->uid);
    memcpy(b->private_key, r.private_key, 32);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
    b->min_revision = r.min_revision;
    b->min_serial = r.min_serial;
    crypto_wipe(&r, sizeof r);
    return 0;
#endif
}

int lfAPI_enroll(void) {
    static record_t r;
    uint8_t id[8], pub[32];
    if (!record_read(&r)) {
        memset(&r, 0, sizeof r);
        r.magic = RECORD_MAGIC;
        if (random_key(r.private_key) != 0) {
            crypto_wipe(&r, sizeof r);
            lf_puts("enroll: the RNG failed its checks; no key made");
            lfCR();
            return ERR_UNSUPPORTED_OPERATION;
        }
        int ior = record_write(&r);
        if (ior || !record_read(&r)) {
            crypto_wipe(&r, sizeof r);
            lf_puts("enroll: can't write the key record");
            lfCR();
            return ior ? ior : ERR_FLASH_WRITE_SECTOR;
        }
        lf_puts("enroll: new key made. ");
    }
    crypto_x25519_public_key(pub, r.private_key);
    crypto_wipe(&r, sizeof r);
    uid(id);
    lf_puts("boards.csv line:");
    lfCR();
    put_hex(id, 8);
    lf_puts(",");
    put_hex(pub, 32);
    lfCR();
    return 0;
}
