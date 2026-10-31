/*
 * The ECDC engine in CTR mode (ecdc.h), the hardware path of ctr.c
 * (ctr_hw_crypt), and `ecdc-test`.
 *
 * The engine is a RAM-to-RAM bus master, and the PMP can't bind a bus
 * master, so under the clock fence (SYS-SEC-13, doc/admin/firmware-updates.md
 * section 8 part 2) user mode may not run it. The engine driver and the
 * ecall service (ecdc_serve) therefore run in machine mode (MMODE, in flash
 * where user mode can't write them: mmode.h), point the engine only at
 * their own DTCM buffer, and turn its clock on for the one transfer and off
 * again. ctr_hw_crypt and the engine's self-check go through the service.
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
 *   worked, and a first check on plain uint32_t arrays failed (and the
 *   board hung later, as if the engine had written elsewhere). So the
 *   engine only ever sees ecdc_buf, which is 32-byte aligned.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "ecdc.h"
#include "mmode.h"                      /* MMODE: the engine driver runs from flash */
#include "guard.h"                      /* guard_buffer_ok */
#include "pmp.h"                        /* pmp_call, pmp_in_ram, PMP_SVC_ECDC */
#include "lftime.h"                     /* lfGetTimeMicroSec (the test's timing only) */
#define CTR_HW 1                        /* the prototype of ctr_hw_crypt, as lf_ctr.c has it */
#include "../../../../ctr.h"
#include "../../../../errcodes.h"
#include "../../../../tools.h"          /* lf_puts, lfCR (ecdc-test) */

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
#define TIM6_WRAP       1000u           /* TIM6->CNT counts 0..999 (lftime.c) */
#define ALIGNED         __attribute__((aligned(32)))

/* ------------------------------------------------------------------------
 * Machine mode: the engine driver and the ecall service. MMODE, so they run
 * from flash and call only other machine-mode code (mmodecheck.py). No
 * memcpy, printf or lfGetTimeMicroSec (not machine-mode; and TIM6's
 * interrupt doesn't run inside an ecall handler, so time is read from
 * TIM6->CNT directly, as lf_standby_service does).
 */

/* The only memory the engine ever points at: 32-byte aligned, in DTCM */
static uint32_t ecdc_buf[ECDC_BUF_BYTES / 4] ALIGNED;

/* Link_v5f.ld: DTCM's .data and .bss up to the stack, and memalloc.c's pool,
   the regions a user buffer may lie in (the same as gdma.c's) */
extern uint32_t _data_vma[], _susrstack[], _pool_start[], _pool_end[];

MMODE static uint32_t ecdc_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Microseconds since *prev, from TIM6->CNT; one call is far under a
   millisecond, so the counter wraps at most once between reads */
MMODE static uint32_t us_step(uint32_t *prev) {
    uint32_t now = TIM6->CNT;
    uint32_t d = (now >= *prev) ? now - *prev : now + TIM6_WRAP - *prev;
    *prev = now;
    return d;
}

/* Waits for flag in INT_FG and clears it; returns 0 or ERR_BLK_TIMEOUT */
MMODE static int ecdc_wait(uint32_t flag) {
    uint32_t prev = TIM6->CNT, us = 0;
    while (!(ECDC->INT_FG & flag)) {
        us += us_step(&prev);
        if (us > WAIT_US) {
            return ERR_BLK_TIMEOUT;
        }
    }
    ECDC->INT_FG = flag;
    return 0;
}

/* Busy-waits us_wanted microseconds. The guard count stops a hang if TIM6
   is somehow not running (us would never grow). */
MMODE static void ecdc_delay(uint32_t us_wanted) {
    uint32_t prev = TIM6->CNT, us = 0, guard = 0;
    while (us < us_wanted && guard < 10000000u) {
        us += us_step(&prev);
        guard++;
    }
}

/* Word copy, without calling memcpy (this runs in machine mode, from flash,
   and must call nothing user mode can write). Both sides are word aligned. */
MMODE static void ecdc_copy(volatile uint32_t *d, const volatile uint32_t *s, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        d[i] = s[i];
    }
}

/* Runs units 16-byte units in place on ecdc_buf: clock on, reset, load the
   key and counter, expand the key and pause, run RAM-to-RAM, then clear the
   key and counter and drop the clock. Returns 0 or ERR_BLK_TIMEOUT. */
MMODE static int ecdc_engine(int cipher, const uint8_t key[16], const uint8_t counter[16],
                             uint32_t units) {
    RCC->HB2PCENR |= (uint32_t)RCC_HB2Periph_ECDC;      /* clock on */
    RCC->HB2PRSTR |= (uint32_t)RCC_HB2Periph_ECDC;      /* reset, or key expansion may not run */
    RCC->HB2PRSTR &= ~(uint32_t)RCC_HB2Periph_ECDC;

    uint32_t ctrl = CTRL_CLKEN | CTRL_CLKSEL | CTRL_CTR | (cipher == ECDC_AES ? CTRL_AES : 0);
    ECDC->INT_FG = FLAGS;
    ECDC->CTRL = ctrl;
    ECDC->KEY_255T224 = ECDC->KEY_223T192 = ECDC->KEY_191T160 = ECDC->KEY_159T128 = 0;
    ECDC->KEY_127T96 = ecdc_be32(key);
    ECDC->KEY_95T64 = ecdc_be32(key + 4);
    ECDC->KEY_63T32 = ecdc_be32(key + 8);
    ECDC->KEY_31T0 = ecdc_be32(key + 12);
    ECDC->IV_127T96 = ecdc_be32(counter);
    ECDC->IV_95T64 = ecdc_be32(counter + 4);
    ECDC->IV_63T32 = ecdc_be32(counter + 8);
    ECDC->IV_31T0 = ecdc_be32(counter + 12);

    ECDC->CTRL = ctrl | CTRL_KEYEX;
    int err = ecdc_wait(FLAG_EKDONE);
    ECDC->CTRL = ctrl;
    if (!err) {
        ecdc_delay(SETTLE_US);                          /* the last round key */
        ECDC->CTRL = ctrl | CTRL_WRSRAM | CTRL_NORMAL;  /* RAM-to-RAM */
        ECDC->DST_ADDR = (uint32_t)ecdc_buf;            /* the engine's own buffer only */
        ECDC->SRC_ADDR = (uint32_t)ecdc_buf;
        ECDC->SRAM_LEN = units;                         /* starts it */
        err = ecdc_wait(FLAG_RAM);
        ECDC->CTRL = ctrl;
    }
    if (err) {                                          /* stop its DMA */
        RCC->HB2PRSTR |= (uint32_t)RCC_HB2Periph_ECDC;
        RCC->HB2PRSTR &= ~(uint32_t)RCC_HB2Periph_ECDC;
    }
    ECDC->KEY_127T96 = ECDC->KEY_95T64 = ECDC->KEY_63T32 = ECDC->KEY_31T0 = 0;
    ECDC->IV_127T96 = ECDC->IV_95T64 = ECDC->IV_63T32 = ECDC->IV_31T0 = 0;
    ECDC->CTRL = 0;
    RCC->HB2PCENR &= ~(uint32_t)RCC_HB2Periph_ECDC;     /* clock off */
    return err;
}

/* Nonzero if the user buffer [p, p + n) is one the service may read and
   write: in DTCM's .data/.bss or in the pool, never the engine's buffer
   (the same rule as gdma.c) */
MMODE static int ecdc_buf_ok(uint32_t p, uint32_t n) {
    uint32_t x = (uint32_t)ecdc_buf;
    return guard_buffer_ok(p, n, (uint32_t)_data_vma, (uint32_t)_susrstack,
                           x, x + ECDC_BUF_BYTES)
        || guard_buffer_ok(p, n, (uint32_t)_pool_start, (uint32_t)_pool_end, 0, 0);
}

int32_t ecdc_serve(uint32_t reqp);
MMODE int32_t ecdc_serve(uint32_t reqp) {
    if ((reqp & 3u) || !pmp_in_ram(reqp, sizeof(ecdc_req))) {
        return ERR_INVALID_ADDRESS;
    }
    const volatile ecdc_req *r = (const volatile ecdc_req *)reqp;
    uint32_t cipher = r->cipher, buf = r->buf, bytes = r->bytes;
    if ((cipher != ECDC_AES && cipher != ECDC_SM4) || bytes == 0 || bytes > ECDC_BUF_BYTES
        || (bytes & 15u)) {
        return ERR_INVALID_NUMERIC;
    }
    if ((buf & 3u) || !ecdc_buf_ok(buf, bytes)) {
        return ERR_INVALID_ADDRESS;
    }
    volatile uint8_t key[16], ctr[16];                  /* volatile: the clears below
                                                           don't become a memset call,
                                                           which .mmode code can't make */
    for (int i = 0; i < 16; i++) {
        key[i] = r->key[i];
        ctr[i] = r->counter[i];
    }
    ecdc_copy(ecdc_buf, (const volatile uint32_t *)buf, bytes / 4);
    int err = ecdc_engine((int)cipher, (const uint8_t *)key, (const uint8_t *)ctr, bytes / 16);
    if (!err) {
        ecdc_copy((volatile uint32_t *)buf, ecdc_buf, bytes / 4);
    }
    volatile uint32_t *z = ecdc_buf;                    /* zero the engine buffer */
    for (uint32_t i = 0; i < ECDC_BUF_BYTES / 4; i++) {
        z[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        key[i] = 0;
        ctr[i] = 0;
    }
    return err;
}

/* ------------------------------------------------------------------------
 * User mode: ctr.c's hardware path, through the service. Checked once
 * against software for each cipher.
 */
static int checked[3];                  /* by ctr cipher: 0 not yet, 1 good, -1 off */

/* Fills a request and calls the machine-mode service. The key is copied
   onto the stack in the request and wiped after. Returns the service's ior. */
static int ecdc_run(int cipher, const uint8_t key[16], const uint8_t counter[16],
                    uint8_t *buf, uint32_t bytes) {
    ecdc_req req;
    for (int i = 0; i < 16; i++) {
        req.key[i] = key[i];
        req.counter[i] = counter[i];
    }
    req.cipher = (uint32_t)cipher;
    req.buf = (uint32_t)buf;
    req.bytes = bytes;
    int r = pmp_call(PMP_SVC_ECDC, (uint32_t)&req, 0);
    for (int i = 0; i < 16; i++) {
        req.key[i] = 0;
        req.counter[i] = 0;
    }
    return r;
}

/* Runs 16 units through the engine (via the service) and through software;
   1 if they match */
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
    int ok = ecdc_run(cipher == CTR_SM4 ? ECDC_SM4 : ECDC_AES, key, c,
                      (uint8_t *)b, sizeof b) == 0
             && memcmp(a, b, sizeof a) == 0;
    ctr_wipe(&k);
    if (!ok) {                          /* only then: at 9600 baud a line costs 30 ms */
        printf("V5F ecdc: %s failed its check: software from now on\r\n",
               cipher == CTR_SM4 ? "SM4" : "AES-128");
    }
    return ok;
}

int ctr_hw_crypt(const ctr_key *k, const uint8_t counter[16], uint8_t *buf, size_t len) {
    if ((k->cipher != CTR_AES128 && k->cipher != CTR_SM4)
        || len == 0 || len > ECDC_BUF_BYTES || (len & 15u)) {
        return -1;                      /* software (which never fails) */
    }
    if (checked[k->cipher] == 0) {
        checked[k->cipher] = check_engine(k->cipher) ? 1 : -1;
    }
    if (checked[k->cipher] < 0) {
        return -1;
    }
    int cipher = (k->cipher == CTR_SM4) ? ECDC_SM4 : ECDC_AES;
    if (ecdc_run(cipher, k->key, counter, buf, (uint32_t)len) != 0) {
        /* the service copies back only on success, so buf is untouched:
           software redoes it, and the engine is off for this cipher now */
        checked[k->cipher] = -1;
        printf("V5F ecdc: %s failed: software from now on\r\n",
               cipher == ECDC_SM4 ? "SM4" : "AES-128");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * ecdc-test
 */
#define UNITS 256                       /* a 4 KB block */
static uint32_t plain[UNITS * 4] ALIGNED;
static uint32_t soft[UNITS * 4] ALIGNED;
static uint32_t hard[UNITS * 4] ALIGNED;

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
        say(": failed (ior ");
        say_u((uint32_t)-err);
        say(")");
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

/* The counter block for the first unit of block TEST_BLOCK */
static void test_counter(uint8_t c0[16]) {
    uint64_t count = (uint64_t)TEST_BLOCK * 256;
    memcpy(c0, test_nonce, 8);
    for (int i = 0; i < 8; i++) {
        c0[8 + i] = (uint8_t)(count >> (56 - 8 * i));
    }
}

/* The engine directly, through the service */
static void run(const char *what, int ctr_cipher) {
    ctr_key k;
    uint8_t c0[16];
    int ecipher = (ctr_cipher == CTR_SM4) ? ECDC_SM4 : ECDC_AES;
    ctr_setkey(&k, ctr_cipher, test_key);
    test_counter(c0);
    memcpy(soft, plain, sizeof soft);
    uint64_t t0 = lfGetTimeMicroSec();
    ctr_crypt_block_sw(&k, test_nonce, TEST_BLOCK, (uint8_t *)soft, sizeof soft);
    uint32_t t_soft = (uint32_t)(lfGetTimeMicroSec() - t0);
    memcpy(hard, plain, sizeof hard);
    t0 = lfGetTimeMicroSec();
    int err = ecdc_run(ecipher, test_key, c0, (uint8_t *)hard, sizeof hard);
    uint32_t t_hard = (uint32_t)(lfGetTimeMicroSec() - t0);
    report(what, err, t_hard, t_soft);
    ctr_wipe(&k);
}

/* The path packages take: ctr_crypt_block, which uses the engine for whole
   aligned blocks and falls back to software */
static void run_ctr(const char *what, int ctr_cipher) {
    ctr_key k;
    ctr_setkey(&k, ctr_cipher, test_key);
    memcpy(soft, plain, sizeof soft);
    uint64_t t0 = lfGetTimeMicroSec();
    ctr_crypt_block_sw(&k, test_nonce, TEST_BLOCK, (uint8_t *)soft, sizeof soft);
    uint32_t t_soft = (uint32_t)(lfGetTimeMicroSec() - t0);
    memcpy(hard, plain, sizeof hard);
    t0 = lfGetTimeMicroSec();
    ctr_crypt_block(&k, test_nonce, TEST_BLOCK, (uint8_t *)hard, sizeof hard);
    uint32_t t_hard = (uint32_t)(lfGetTimeMicroSec() - t0);
    report(what, 0, t_hard, t_soft);
    ctr_wipe(&k);
}

int lfAPI_ecdcTest(void) {
    for (int i = 0; i < UNITS * 4; i++) {
        plain[i] = (uint32_t)i * 0x9E3779B9u;
    }
    run("AES via the service", CTR_AES128);
    run("SM4 via the service", CTR_SM4);
    run_ctr("AES via ctr_crypt_block (first use: checks the engine)", CTR_AES128);
    run_ctr("AES via ctr_crypt_block again", CTR_AES128);
    run_ctr("SM4 via ctr_crypt_block (first use)", CTR_SM4);
    say("ctr_crypt_block uses the engine for AES: ");
    say(checked[CTR_AES128] > 0 ? "yes" : "no");
    say(", SM4: ");
    say(checked[CTR_SM4] > 0 ? "yes" : "no");
    lfCR();
    return 0;
}
