/*
 * The ECDC engine in CTR mode (ecdc.h), and `ecdc-test`.
 *
 * What the registers do, measured from Forth (doc/ch32h417-notes.md):
 * CTRL bit 9 set is CTR, bit 8 set AES (clear SM4), bit 13 (DAT_MOD) set
 * makes key, counter and data natural big-endian words, with KEY_127T96,
 * IV_127T96 and so on holding bytes 0-3. The counter loads at key
 * expansion, and key expansion is reliable only after a reset. Flags are
 * INT_FG bits 16-18, cleared by writing 1s.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "ecdc.h"
#include "lftime.h"
#include "../../../../ctr.h"
#include "../../../../errcodes.h"
#include "../../../../tools.h"

#define CTRL_KEYEX      (1u << 0)
#define CTRL_NORMAL     (1u << 1)
#define CTRL_DIV_SHIFT  4
#define CTRL_WRSRAM     (1u << 7)
#define CTRL_AES        (1u << 8)
#define CTRL_CTR        (1u << 9)       /* measured: set is CTR (the SDK says ECB) */
#define CTRL_BIG        (1u << 13)      /* DAT_MOD: natural byte order */
#define CTRL_CLKSEL     (1u << 24)      /* PLLCLK undivided */
#define CTRL_CLKEN      (1u << 25)
#define FLAG_EKDONE     (1u << 16)
#define FLAG_SINGLE     (1u << 17)
#define FLAG_RAM        (1u << 18)
#define FLAGS           (FLAG_EKDONE | FLAG_SINGLE | FLAG_RAM)
#define WAIT_US         10000u          /* far longer than any step takes */

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Waits for flag in INT_FG; returns 0 or ERR_BLK_TIMEOUT */
static int wait_flag(uint32_t flag) {
    uint64_t t0 = lfGetTimeMicroSec();
    while (!(ECDC->INT_FG & flag)) {
        if (lfGetTimeMicroSec() - t0 > WAIT_US) {
            return ERR_BLK_TIMEOUT;
        }
    }
    ECDC->INT_FG = flag;
    return 0;
}

int ecdc_ctr(int cipher, const uint8_t key[16], const uint8_t counter[16],
             const void *src, void *dst, uint32_t units, int div) {
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ECDC, ENABLE);
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, ENABLE);     /* or key expansion may not run */
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, DISABLE);

    uint32_t ctrl = CTRL_CLKEN | CTRL_CTR | CTRL_BIG | (cipher == ECDC_AES ? CTRL_AES : 0);
    ctrl |= (div <= 1) ? CTRL_CLKSEL : ((uint32_t)(div & 7) << CTRL_DIV_SHIFT);
    ECDC->INT_FG = FLAGS;
    ECDC->CTRL = ctrl;
    ECDC->KEY_255T224 = ECDC->KEY_223T192 = ECDC->KEY_191T160 = ECDC->KEY_159T128 = 0;
    ECDC->KEY_127T96 = be32(key);
    ECDC->KEY_95T64 = be32(key + 4);
    ECDC->KEY_63T32 = be32(key + 8);
    ECDC->KEY_31T0 = be32(key + 12);
    ECDC->IV_127T96 = be32(counter);
    ECDC->IV_95T64 = be32(counter + 4);
    ECDC->IV_63T32 = be32(counter + 8);
    ECDC->IV_31T0 = be32(counter + 12);

    ECDC->CTRL = ctrl | CTRL_KEYEX;
    int err = wait_flag(FLAG_EKDONE);
    ECDC->CTRL = ctrl;
    if (err) {
        return err;
    }
    ECDC->CTRL = ctrl | CTRL_WRSRAM | CTRL_NORMAL;          /* RAM-to-RAM, encrypt */
    ECDC->DST_ADDR = (uint32_t)dst;
    ECDC->SRC_ADDR = (uint32_t)src;
    ECDC->SRAM_LEN = units;                                 /* starts it, as in WCH's example */
    err = wait_flag(FLAG_RAM);
    ECDC->CTRL = ctrl;
    return err;
}

/* ------------------------------------------------------------------------
 * ecdc-test
 */
#define UNITS 256                       /* a 4 KB block */
static uint32_t plain[UNITS * 4] __attribute__((aligned(32)));
static uint32_t soft[UNITS * 4] __attribute__((aligned(32)));
static uint32_t hard[UNITS * 4] __attribute__((aligned(32)));

static const uint8_t test_key[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
static const uint8_t test_nonce[8] = { 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7 };
#define TEST_BLOCK 5

static void say(const char *s) {
    lf_puts(s);
}

static void say_u(uint32_t v) {
    char b[12];
    snprintf(b, sizeof b, "%lu", (unsigned long)v);
    lf_puts(b);
}

/* The keystream of unit i: hard xor plain */
static void keystream(const uint32_t *out, int i, uint8_t ks[16]) {
    const uint8_t *o = (const uint8_t *)&out[i * 4], *p = (const uint8_t *)&plain[i * 4];
    for (int j = 0; j < 16; j++) {
        ks[j] = o[j] ^ p[j];
    }
}

/* Says how the engine's counter went when it doesn't match software */
static void diagnose(const ctr_key *k, const uint8_t c0[16]) {
    uint8_t ks0[16], ks1[16], e[16], c[16];
    keystream(hard, 0, ks0);
    keystream(hard, 1, ks1);
    ctr_encrypt_block(k, c0, e);
    if (memcmp(ks0, e, 16)) {
        say("    unit 0 isn't E(counter): the key or counter didn't load");
        lfCR();
        return;
    }
    if (!memcmp(ks1, ks0, 16)) {
        say("    every unit used the first counter: the engine doesn't step it");
        lfCR();
        return;
    }
    memcpy(c, c0, 16);                  /* little-endian step: byte 0 counts */
    for (int j = 0; j < 16 && ++c[j] == 0; j++) {
    }
    ctr_encrypt_block(k, c, e);
    if (!memcmp(ks1, e, 16)) {
        say("    unit 1 is E(counter stepped little-endian, from byte 0)");
        lfCR();
        return;
    }
    memcpy(c, c0, 16);                  /* the low 32-bit word only */
    uint32_t w = be32(c + 12) + 1;
    c[12] = (uint8_t)(w >> 24); c[13] = (uint8_t)(w >> 16); c[14] = (uint8_t)(w >> 8); c[15] = (uint8_t)w;
    ctr_encrypt_block(k, c, e);
    say(!memcmp(ks1, e, 16) ? "    unit 1 is E(counter + 1) (32-bit step)"
                            : "    unit 1 is something else");
    lfCR();
}

static void run(int cipher, int div, int in_place) {
    ctr_key k;
    uint8_t c0[16];
    ctr_setkey(&k, cipher == ECDC_AES ? CTR_AES128 : CTR_SM4, test_key);
    memcpy(c0, test_nonce, 8);
    uint64_t count = (uint64_t)TEST_BLOCK * 256;
    for (int i = 0; i < 8; i++) {
        c0[8 + i] = (uint8_t)(count >> (56 - 8 * i));
    }

    memcpy(soft, plain, sizeof soft);
    uint64_t t0 = lfGetTimeMicroSec();
    ctr_crypt_block(&k, test_nonce, TEST_BLOCK, (uint8_t *)soft, sizeof soft);
    uint32_t t_soft = (uint32_t)(lfGetTimeMicroSec() - t0);

    memset(hard, 0x55, sizeof hard);
    const void *src = plain;
    if (in_place) {
        memcpy(hard, plain, sizeof hard);
        src = hard;
    }
    t0 = lfGetTimeMicroSec();
    int err = ecdc_ctr(cipher, test_key, c0, src, hard, UNITS, div);
    uint32_t t_hard = (uint32_t)(lfGetTimeMicroSec() - t0);

    int match = 0, first_bad = -1;
    for (int i = 0; i < UNITS; i++) {
        if (!memcmp(&hard[i * 4], &soft[i * 4], 16)) {
            match++;
        } else if (first_bad < 0) {
            first_bad = i;
        }
    }
    say(cipher == ECDC_AES ? "AES" : "SM4");
    say(" div ");
    say_u((uint32_t)div);
    say(in_place ? ", in place: " : ", out of place: ");
    if (err) {
        say("timed out (ior ");
        say_u((uint32_t)-err);
        say(")");
        lfCR();
    } else {
        say_u((uint32_t)match);
        say("/256 units match software; engine ");
        say_u(t_hard);
        say(" us, software ");
        say_u(t_soft);
        say(" us");
        if (first_bad >= 0) {
            say("; first mismatch at unit ");
            say_u((uint32_t)first_bad);
        }
        lfCR();
        if (match == 0) {
            diagnose(&k, c0);
        }
    }
    ctr_wipe(&k);
}

int lfAPI_ecdcTest(void) {
    for (int i = 0; i < UNITS * 4; i++) {
        plain[i] = (uint32_t)i * 0x9E3779B9u;
    }
    run(ECDC_AES, 1, 0);
    run(ECDC_AES, 6, 0);
    run(ECDC_AES, 1, 1);
    run(ECDC_SM4, 1, 0);
    run(ECDC_SM4, 1, 1);
    say("buffers at ");
    char b[12];
    snprintf(b, sizeof b, "%08lX", (unsigned long)(uintptr_t)plain);
    say(b);
    lfCR();
    return 0;
}
