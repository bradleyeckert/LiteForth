/*
 * What the V5F knows about this board (board.h). The board's private key
 * isn't here: stage 1 on the V3F holds it, below the V5F's PMP entry 0
 * (doc/admin/firmware-updates.md section 8), and gives the V5F only its
 * public key, for `enroll`.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "board.h"
#include "cdc_shared.h"             /* enroll, board_pub (Common/) */
#include "lftime.h"                 /* lfGetTimeMicroSec */
#include "../../../../errcodes.h"
#include "../../../../tools.h"
#include "security.h"               /* SECURITY_LEVEL (Common/) */
#if __has_include("sealed_root.h")
#include "sealed_root.h"            /* SEALED_ROOT_KEY, SEALED_PRODUCT */
#include "security.h"               /* again: no old flags in sealed_root.h */
#endif

#define UID_ADDR        0x1FFFF7E8u         /* ESIG UNIID1, then UNIID2 */
#define ENROLL_WAIT_US  2000000u            /* stage 1 answers from its USB loop */

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

int board_get(sealed_board *b, char *msg, int len) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    snprintf(msg, len, "no root key built in (V5F/User/sealed_root.h)");
    return ERR_UNSUPPORTED_OPERATION;
#else
    (void)msg;
    (void)len;
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    uid(b->uid);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
    return 0;
#endif
}

int lfAPI_enroll(void) {
    uint8_t id[8], pub[32];
    cdc_shared_t *sh = CDC_SHARED;
    sh->enroll = CDC_ENROLL_ASK;
    CDC_FENCE();
    uint64_t t0 = lfGetTimeMicroSec();
    uint32_t got;
    while ((got = sh->enroll) == CDC_ENROLL_ASK) {
        if (lfGetTimeMicroSec() - t0 > ENROLL_WAIT_US) {
            sh->enroll = 0;
            lf_puts("enroll: stage 1 didn't answer");
            lfCR();
            return ERR_UNSUPPORTED_OPERATION;
        }
        cdc_backoff();
    }
    CDC_FENCE();
    if (got != CDC_ENROLL_DONE) {
        sh->enroll = 0;
        lf_puts("enroll: this board has no key yet. Make it in stage 1's recovery: "
                "Ctrl+X three times, twice, then enroll");
        lfCR();
        return ERR_UNSUPPORTED_OPERATION;
    }
    for (int i = 0; i < 32; i++) {
        pub[i] = sh->board_pub[i];
    }
    sh->enroll = 0;
#if SEC_UID_KEY
    lf_puts("(security level below 2: the key comes from the UID, not the RNG) ");
#endif
    uid(id);
    lf_puts("boards.csv line:");
    lfCR();
    put_hex(id, 8);
    lf_puts(",");
    put_hex(pub, 32);
    lfCR();
    return 0;
}
