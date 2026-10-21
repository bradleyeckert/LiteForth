/*
 * The board's record for firmware update packages (board.h): an 8 KB flash
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
 * Below SECURITY_LEVEL 2 (Common/security.h, development only), the key
 * is BLAKE2s of the UID instead (personalization "LFinsec1"), used whether
 * or not there is a record (which then only holds the minimums), so every
 * `enroll` on a board gives the same key, even after the record is erased,
 * and `sealblocks.py board-key OUT --uid UID --insecure` knows it without
 * the board. Anyone who knows a board's UID can read its files.
 *
 * The private key stays in machine mode (doc/admin/firmware-updates.md section
 * 8): the PMP keeps user mode, where LiteForth runs, out of this sector,
 * and board_serve, reached by pmp.c's ecall service, does everything that
 * needs the key: it checks the record, makes and writes the key
 * (`enroll`), and gives the public key and the shared secret for a sealed
 * file's ephemeral key (sealed_board.x25519). User mode only ever sees
 * the public key, the minimums and those shared secrets.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "board.h"
#include "flash.h"
#include "pmp.h"                    /* the record, through machine mode */
#include "../../../../blake2s.h"
#include "../../../../monocypher.h"
#include "../../../../errcodes.h"
#include "../../../../tools.h"
#include "security.h"               /* SECURITY_LEVEL (Common/) */
#if __has_include("sealed_root.h")
#include "sealed_root.h"            /* SEALED_ROOT_KEY, SEALED_PRODUCT */
#include "security.h"               /* again: no old flags in sealed_root.h */
#endif

#define RECORD_BYTES    256u
#define RECORD_MAGIC    0x314B464Cu         /* "LFK1" */
#define UID_ADDR        0x1FFFF7E8u         /* ESIG UNIID1, then UNIID2 */
#define RNG_WORDS       64
#define RNG_WAIT        100000              /* polls for a word before giving up */
#define SECTOR_BYTES    8192u

extern const uint32_t __lf_flash_start[];   /* Link_v5f.ld */
extern const uint8_t  __lf_flash_size[];
#define RECORD_ADDR     ((uint32_t)__lf_flash_start + (uint32_t)__lf_flash_size)

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

/* ---- Machine mode: board_serve, through pmp.c's ecall ------------------ */

static void record_check(const record_t *r, uint8_t out[16]) {
    blake2s_state S;
    static const uint8_t person[8] = "LFrecord";
    b2s_init(&S, 16, NULL, 0, NULL, person);
    b2s_puts(&S, (const uint8_t *)r, 48);
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
}

/* Copies the record from flash (PMP entry 1: machine mode only); returns
   1 if it's valid */
static int record_read(record_t *r) {
    uint8_t check[16];
    memcpy(r, (const void *)RECORD_ADDR, sizeof *r);
    record_check(r, check);
    int ok = r->magic == RECORD_MAGIC && memcmp(check, r->check, 16) == 0;
    crypto_wipe(check, sizeof check);
    return ok;
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

#if SEC_UID_KEY
/* Levels 0 and 1: the private key from the UID, the same every time */
static int random_key(uint8_t key[32]) {
    static const uint8_t person[8] = "LFinsec1";
    blake2s_state S;
    uint8_t id[8];
    memcpy(id, (const void *)UID_ADDR, 8);
    b2s_init(&S, 32, NULL, 0, NULL, person);
    b2s_puts(&S, id, 8);
    b2s_final(&S, key);
    crypto_wipe(&S, sizeof S);
    return 0;
}
#else
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
            crypto_wipe(&S, sizeof S);
            return -1;
        }
        uint32_t x = RNG_GetRandomNumber();
        if (i && x == last) {
            crypto_wipe(&S, sizeof S);
            return -1;                      /* stuck */
        }
        last = x;
        b2s_puts(&S, (const uint8_t *)&x, 4);
    }
    b2s_final(&S, key);
    crypto_wipe(&S, sizeof S);
    return 0;
}
#endif

/* The private key into key, and the record's minimums into info: from
   the UID below level 2 (with or without a record), else from a valid
   record. Returns 1 if there's a key. */
static int key_get(uint8_t key[32], board_info *info) {
    record_t r;
    int valid = record_read(&r);
    info->valid = (uint32_t)valid;
    info->min_revision = valid ? r.min_revision : 0;
    info->min_serial = valid ? r.min_serial : 0;
#if SEC_UID_KEY
    random_key(key);                    /* whatever the record holds */
    int have = 1;
#else
    int have = valid;
    if (valid) {
        memcpy(key, r.private_key, 32);
    }
#endif
    crypto_wipe(&r, sizeof r);
    return have;
}

static int serve_info(board_info *info) {
    uint8_t key[32];
    memset(info, 0, sizeof *info);
    if (key_get(key, info)) {
        info->has_key = 1;
        crypto_x25519_public_key(info->public_key, key);
    }
    crypto_wipe(key, sizeof key);
    return 0;
}

static int serve_enroll(board_info *info) {
    record_t r;
    int made = 0;
    if (!record_read(&r)) {
        memset(&r, 0, sizeof r);
        r.magic = RECORD_MAGIC;
        if (random_key(r.private_key) != 0) {
            crypto_wipe(&r, sizeof r);
            return ERR_UNSUPPORTED_OPERATION;
        }
        int ior = record_write(&r);
        if (ior) {
            crypto_wipe(&r, sizeof r);
            return ior;
        }
        made = 1;
    }
    crypto_wipe(&r, sizeof r);
    serve_info(info);
    info->made = (uint32_t)made;
    return info->valid ? 0 : ERR_FLASH_WRITE_SECTOR;
}

static int serve_x25519(uint8_t shared[32], const uint8_t their_public[32]) {
    board_info info;
    uint8_t key[32];
    int have = key_get(key, &info);
    if (have) {
        crypto_x25519(shared, key, their_public);
    }
    crypto_wipe(key, sizeof key);
    crypto_wipe(&info, sizeof info);
    return have ? 0 : ERR_UNSUPPORTED_OPERATION;
}

/* The services' mailbox. Machine mode reads and writes only this, never a
   buffer user mode points it at: the ecall handler's frames are on user
   mode's stack, so a pointer just below the caller's sp would have it
   overwrite its own return address. The user-mode wrappers below copy in
   and out, and wipe it. */
static union {
    board_info info;
    struct {
        uint8_t their[32];              /* in: their public key */
        uint8_t shared[32];             /* out: the shared secret */
    } x;
} mail;

int board_serve(uint32_t n, uint32_t a, uint32_t b) {
    (void)a;
    (void)b;
    switch (n) {
    case BOARD_SVC_INFO:
        return serve_info(&mail.info);
    case BOARD_SVC_ENROLL:
        return serve_enroll(&mail.info);
    case BOARD_SVC_X25519: {
        uint8_t their[32];
        memcpy(their, mail.x.their, 32);
        int ior = serve_x25519(mail.x.shared, their);
        crypto_wipe(their, sizeof their);
        return ior;
    }
    default:
        return ERR_UNSUPPORTED_OPERATION;
    }
}

/* ---- User mode --------------------------------------------------------- */

/* Calls BOARD_SVC_INFO or BOARD_SVC_ENROLL; *info gets the mailbox's */
static int info_service(uint32_t n, board_info *info) {
    memset(&mail, 0, sizeof mail);
    int ior = pmp_call(n, 0, 0);
    *info = mail.info;
    crypto_wipe(&mail, sizeof mail);
    return ior;
}

static void uid(uint8_t out[8]) {
    memcpy(out, (const void *)UID_ADDR, 8);
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

#ifdef SEALED_ROOT_KEY
/* sealed_board.x25519: the shared secret from machine mode */
static int x25519_service(uint8_t shared[32], const uint8_t their_public[32]) {
    memset(&mail, 0, sizeof mail);
    memcpy(mail.x.their, their_public, 32);
    int ior = pmp_call(BOARD_SVC_X25519, 0, 0);
    memcpy(shared, mail.x.shared, 32);
    crypto_wipe(&mail, sizeof mail);
    return ior;
}
#endif

int board_get(sealed_board *b, char *msg, int len) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    snprintf(msg, len, "no root key built in (V5F/User/sealed_root.h)");
    return ERR_UNSUPPORTED_OPERATION;
#else
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    board_info info;
    if (info_service(BOARD_SVC_INFO, &info) != 0 || !info.has_key) {
        snprintf(msg, len, "this board has no key: run enroll");
        return ERR_UNSUPPORTED_OPERATION;
    }
    uid(b->uid);
    b->x25519 = x25519_service;         /* the private key stays in machine mode */
    memcpy(b->public_key, info.public_key, 32);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
    b->min_revision = info.min_revision;
    b->min_serial = info.min_serial;
    return 0;
#endif
}

int lfAPI_enroll(void) {
    board_info info;
    uint8_t id[8];
    int ior = info_service(BOARD_SVC_ENROLL, &info);
    if (ior == ERR_UNSUPPORTED_OPERATION) {
        lf_puts("enroll: the RNG failed its checks; no key made");
        lfCR();
        return ior;
    }
    if (ior || !info.has_key) {
        lf_puts("enroll: can't write the key record");
        lfCR();
        return ior ? ior : ERR_FLASH_WRITE_SECTOR;
    }
    if (info.made) {
        lf_puts("enroll: new key made. ");
    }
#if SEC_UID_KEY
    lf_puts("(security level below 2: the key comes from the UID, not the RNG) ");
#endif
    uid(id);
    lf_puts("boards.csv line:");
    lfCR();
    put_hex(id, 8);
    lf_puts(",");
    put_hex(info.public_key, 32);
    lfCR();
    return 0;
}
