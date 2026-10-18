/*
 * The ECDC engine in CTR mode (ecdc.h), the hardware path of ctr.c
 * (ctr_hw_crypt), and `ecdc-test`.
 *
 * What the engine does, measured on the board (doc/ch32h417-notes.md):
 * - CTRL bit 9 set is CTR (the SDK header says ECB), bit 8 set AES, clear
 *   SM4. With DAT_MOD (bit 13) clear, RAM-to-RAM mode reads and writes
 *   memory in its own byte order, and the key and counter registers take
 *   natural big-endian words: KEY_127T96 and IV_127T96 hold bytes 0-3.
 * - The counter loads at key expansion and, in RAM-to-RAM mode, steps by
 *   one per 16 bytes.
 * - Key expansion is reliable only after a reset, and its "done" flag
 *   (INT_FG bit 16) comes a little early: AES units started at once had
 *   their last word wrong (a fixed difference, so the last round key
 *   wasn't ready yet), 4 units at divider 1 and 7 at divider 2. A pause
 *   of SETTLE_US after the flag fixes it.
 * - Flags are INT_FG bits 16-18, cleared by writing 1s.
 * - Buffers need more than 4-byte alignment: 32-byte aligned buffers
 *   worked, and the first check here, on plain uint32_t arrays, failed
 *   (and the board hung later, as if the engine had written elsewhere).
 *   So the engine only ever sees ALIGN-aligned buffers of this file.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "ecdc.h"
#include "lftime.h"
#define CTR_HW 1                        /* the prototype of ctr_hw_crypt, as lf_ctr.c has it */
#include "../../../../ctr.h"
#include "../../../../errcodes.h"
#include "../../../../vm.h"             /* vm_memory, RAM_PAGE (options.h) */
#include "../../../../forth.h"          /* F_BLOCKBUFS */
#include "../../../../tools.h"

#define CTRL_KEYEX      (1u << 0)
#define CTRL_NORMAL     (1u << 1)
#define CTRL_WRSRAM     (1u << 7)
#define CTRL_AES        (1u << 8)
#define CTRL_CTR        (1u << 9)       /* measured: set is CTR */
#define CTRL_CLKSEL     (1u << 24)      /* PLLCLK undivided */
#define CTRL_CLKEN      (1u << 25)
#define FLAG_EKDONE     (1u << 16)
#define FLAG_SINGLE     (1u << 17)
#define FLAG_RAM        (1u << 18)
#define FLAGS           (FLAG_EKDONE | FLAG_SINGLE | FLAG_RAM)
#define WAIT_US         10000u          /* far longer than any step takes */
#define SETTLE_US       5u              /* after EKDONE: 20 measured good, under 1 needed */
#define MAX_UNITS       256u            /* 16-byte units per transfer: a 4 KB block */
#define ALIGNED         __attribute__((aligned(32)))

static uint32_t settle_us = SETTLE_US;  /* ecdc-test changes it */

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Waits for flag in INT_FG and clears it; returns 0 or ERR_BLK_TIMEOUT */
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
             const void *src, void *dst, uint32_t units) {
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ECDC, ENABLE);
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, ENABLE);     /* or key expansion may not run */
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, DISABLE);

    uint32_t ctrl = CTRL_CLKEN | CTRL_CLKSEL | CTRL_CTR | (cipher == ECDC_AES ? CTRL_AES : 0);
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
    uint64_t t0 = lfGetTimeMicroSec();
    while (lfGetTimeMicroSec() - t0 < settle_us) {          /* the last round key */
    }
    ECDC->CTRL = ctrl | CTRL_WRSRAM | CTRL_NORMAL;          /* RAM-to-RAM, encrypt */
    ECDC->DST_ADDR = (uint32_t)dst;
    ECDC->SRC_ADDR = (uint32_t)src;
    ECDC->SRAM_LEN = units;                                 /* starts it */
    err = wait_flag(FLAG_RAM);
    ECDC->CTRL = ctrl;
    if (err) {                                              /* stop its DMA */
        RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, ENABLE);
        RCC_HB2PeriphResetCmd(RCC_HB2Periph_ECDC, DISABLE);
    }
    return err;
}

/* ------------------------------------------------------------------------
 * ctr.c's hardware path: checked once against software for each cipher.
 * A 32-byte aligned buf (the block buffers: memalloc.c aligns the pool and
 * forth.h the buffers in the RAM page) runs in place. Any other buf is
 * copied into scratch, run in place there, and copied back only when the
 * whole run went through, so the engine never sees an unaligned buffer and
 * a failure leaves buf as it was for software to do. A failure in place
 * may have changed buf, so it's reported as such, and the engine is turned
 * off for that cipher from then on.
 */
static int checked[3];                  /* by ctr cipher: 0 not yet, 1 good, -1 off */
static uint32_t scratch[MAX_UNITS * 4] ALIGNED;

/* Runs 16 units through the engine and through software; 1 if they match */
static int check_engine(int cipher) {
    static uint32_t a[64] ALIGNED, b[64] ALIGNED;
    static const uint8_t key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                     0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    static const uint8_t nonce[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    ctr_key k;
    uint8_t c[16];
    for (int i = 0; i < 64; i++) {
        a[i] = (uint32_t)i * 0x01010101u;
    }
    ctr_setkey(&k, cipher, key);
    memcpy(b, a, sizeof a);
    ctr_crypt_block_sw(&k, nonce, 7, (uint8_t *)a, sizeof a);
    memcpy(c, nonce, 8);
    memset(c + 8, 0, 8);
    c[14] = 7;                          /* 7 * 256, big-endian */
    int ok = ecdc_ctr(cipher == CTR_SM4 ? ECDC_SM4 : ECDC_AES, key, c, b, b, 16) == 0
             && memcmp(a, b, sizeof a) == 0;
    ctr_wipe(&k);
    if (!ok) {                          /* only then: at 9600 baud a line costs 30 ms */
        printf("V5F ecdc: %s failed its check: software from now on\r\n",
               cipher == CTR_SM4 ? "SM4" : "AES-128");
    }
    return ok;
}

int ctr_hw_crypt(const ctr_key *k, const uint8_t counter[16], uint8_t *buf, size_t len) {
    if ((k->cipher != CTR_AES128 && k->cipher != CTR_SM4) || len > sizeof scratch) {
        return -1;
    }
    if (checked[k->cipher] == 0) {
        checked[k->cipher] = check_engine(k->cipher) ? 1 : -1;
    }
    if (checked[k->cipher] < 0) {
        return -1;
    }
    int cipher = (k->cipher == CTR_SM4) ? ECDC_SM4 : ECDC_AES;
    if (((uintptr_t)buf & 31) == 0) {   /* no copy */
        if (ecdc_ctr(cipher, k->key, counter, buf, buf, (uint32_t)(len / 16)) == 0) {
            return 0;
        }
        checked[k->cipher] = -1;
        printf("V5F ecdc: %s timed out in place: software from now on\r\n",
               cipher == ECDC_SM4 ? "SM4" : "AES-128");
        return 1;
    }
    memcpy(scratch, buf, len);
    int err = ecdc_ctr(cipher, k->key, counter, scratch, scratch, (uint32_t)(len / 16));
    if (!err) {
        memcpy(buf, scratch, len);
    }
    memset(scratch, 0, len);
    return err ? -1 : 0;                /* on failure buf is untouched: software does it */
}

/* ------------------------------------------------------------------------
 * ecdc-test
 */
#define UNITS 256                       /* a 4 KB block */
static uint32_t plain[UNITS * 4] ALIGNED;
static uint32_t soft[UNITS * 4] ALIGNED;
static uint32_t hard[UNITS * 4] ALIGNED;
static uint32_t odd[UNITS * 4 + 1] ALIGNED;    /* odd + 1: 4-byte aligned, not 32 */

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

/* Compares hard with soft and says how many units match, listing wrong
   words as unit.word^difference */
static void report(const char *what, int err, uint32_t t_hard, uint32_t t_soft) {
    say(what);
    if (err) {
        say(": timed out");
        lfCR();
        return;
    }
    int match = 0, listed = 0;
    char bad[160] = "";
    for (int i = 0; i < UNITS; i++) {
        if (!memcmp(&hard[i * 4], &soft[i * 4], 16)) {
            match++;
            continue;
        }
        for (int w = 0; w < 4 && listed < 12; w++) {
            if (hard[i * 4 + w] != soft[i * 4 + w]) {
                size_t at = strlen(bad);
                snprintf(bad + at, sizeof bad - at, " %d.%d^%08lx", i, w,
                         (unsigned long)(hard[i * 4 + w] ^ soft[i * 4 + w]));
                listed++;
            }
        }
    }
    say(": ");
    say_u((uint32_t)match);
    say("/256 units match software; ");
    say_u(t_hard);
    say(" us, software ");
    say_u(t_soft);
    say(" us");
    lfCR();
    if (match < UNITS) {
        say("    wrong (unit.word^difference):");
        say(bad);
        lfCR();
    }
}

static void run(const char *what, int cipher, int in_place, uint32_t settle, int via_ctr) {
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
    ctr_crypt_block_sw(&k, test_nonce, TEST_BLOCK, (uint8_t *)soft, sizeof soft);
    uint32_t t_soft = (uint32_t)(lfGetTimeMicroSec() - t0);

    int err = 0;
    uint32_t t_copy = 0;
    settle_us = settle;
    if (via_ctr == 1) {                 /* the path sealed files take: an aligned buffer */
        memcpy(hard, plain, sizeof hard);
        t0 = lfGetTimeMicroSec();
        ctr_crypt_block(&k, test_nonce, TEST_BLOCK, (uint8_t *)hard, sizeof hard);
    } else if (via_ctr == 2) {          /* a buffer that isn't: copied through scratch */
        memcpy(odd + 1, plain, sizeof hard);
        t0 = lfGetTimeMicroSec();
        ctr_crypt_block(&k, test_nonce, TEST_BLOCK, (uint8_t *)(odd + 1), sizeof hard);
        t_copy = (uint32_t)(lfGetTimeMicroSec() - t0);
        memcpy(hard, odd + 1, sizeof hard);
    } else {
        memset(hard, 0x55, sizeof hard);
        const void *src = plain;
        if (in_place) {
            memcpy(hard, plain, sizeof hard);
            src = hard;
        }
        t0 = lfGetTimeMicroSec();
        err = ecdc_ctr(cipher, test_key, c0, src, hard, UNITS);
    }
    uint32_t t_hard = via_ctr == 2 ? t_copy : (uint32_t)(lfGetTimeMicroSec() - t0);
    settle_us = SETTLE_US;
    report(what, err, t_hard, t_soft);
    ctr_wipe(&k);
}

int lfAPI_ecdcTest(void) {
    for (int i = 0; i < UNITS * 4; i++) {
        plain[i] = (uint32_t)i * 0x9E3779B9u;
    }
    run("AES, out of place", ECDC_AES, 0, SETTLE_US, 0);
    run("AES, in place", ECDC_AES, 1, SETTLE_US, 0);
    run("AES, no pause after key expansion", ECDC_AES, 0, 0, 0);
    run("SM4, out of place", ECDC_SM4, 0, SETTLE_US, 0);
    run("AES through ctr_crypt_block (first use: checks the engine)", ECDC_AES, 0, SETTLE_US, 1);
    run("AES through ctr_crypt_block again", ECDC_AES, 0, SETTLE_US, 1);
    run("AES through ctr_crypt_block, a buffer not 32-byte aligned", ECDC_AES, 0, SETTLE_US, 2);
    run("SM4 through ctr_crypt_block (first use)", ECDC_SM4, 0, SETTLE_US, 1);
    run("SM4 through ctr_crypt_block again", ECDC_SM4, 0, SETTLE_US, 1);
    run("SM4 through ctr_crypt_block, a buffer not 32-byte aligned", ECDC_SM4, 0, SETTLE_US, 2);
    say("ctr_crypt_block uses the engine for AES: ");
    say(checked[CTR_AES128] > 0 ? "yes" : "no");
    say(", SM4: ");
    say(checked[CTR_SM4] > 0 ? "yes" : "no");
    lfCR();
    say("The block buffers are 32-byte aligned (no copy): ");
    say(((uintptr_t)(vm_memory[RAM_PAGE] + F_BLOCKBUFS) & 31) == 0 ? "yes" : "no");
    lfCR();
    return 0;
}
