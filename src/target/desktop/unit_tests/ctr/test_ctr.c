/* Tests src/ctr.c: the AES-128 (FIPS-197 C.1) and SM4 (GB/T 32907 A.1)
   known answers, and CTR mode on whole and partial blocks against the
   cryptography package (vectors.h; `make vectors` regenerates it). The
   plaintext of vector k is byte i = i * 31 + k. */
#include <stdio.h>
#include <string.h>
#include "ctr.h"
#include "vectors.h"

static int failures;

static void check(int ok, const char *what, int i) {
    if (!ok) {
        printf("FAIL: %s (%d)\n", what, i);
        failures++;
    }
}

static void unhex(const char *s, uint8_t *out) {
    for (int i = 0; i < 16; i++) {
        unsigned x;
        sscanf(s + 2 * i, "%2x", &x);
        out[i] = (uint8_t)x;
    }
}

static void known_answer(int cipher, const char *key, const char *pt, const char *ct) {
    ctr_key k;
    uint8_t kb[16], in[16], want[16], out[16];
    unhex(key, kb); unhex(pt, in); unhex(ct, want);
    check(ctr_setkey(&k, cipher, kb) == 0, "setkey", cipher);
    ctr_encrypt_block(&k, in, out);
    check(memcmp(out, want, 16) == 0, "known answer", cipher);
    ctr_encrypt_block(&k, in, in);                  /* in place */
    check(memcmp(in, want, 16) == 0, "known answer in place", cipher);
}

int main(void) {
    known_answer(CTR_AES128, "000102030405060708090a0b0c0d0e0f",
                 "00112233445566778899aabbccddeeff", "69c4e0d86a7b0430d8cdb78070b4c55a");
    known_answer(CTR_SM4, "0123456789abcdeffedcba9876543210",
                 "0123456789abcdeffedcba9876543210", "681edf34d206965e86b3e94f536e4246");
    ctr_key k;
    uint8_t key[16] = {0};
    check(ctr_setkey(&k, 3, key) == -1, "unknown cipher accepted", 3);

    int n = (int)(sizeof ctr_vectors / sizeof ctr_vectors[0]);
    static uint8_t buf[4096];
    for (int i = 0; i < n; i++) {
        const ctr_vector *v = &ctr_vectors[i];
        int k_index = i % 5;
        for (int j = 0; j < v->len; j++) {
            buf[j] = (uint8_t)(j * 31 + k_index);
        }
        ctr_setkey(&k, v->cipher, (const uint8_t *)v->key);
        ctr_crypt_block(&k, (const uint8_t *)v->nonce, (uint32_t)v->n, buf, (size_t)v->len);
        int first = v->len < 48 ? v->len : 48;
        check(memcmp(buf, v->first, first) == 0, "CTR start", i);
        check(memcmp(buf + v->len - 16, v->last, 16) == 0, "CTR end", i);
        /* and back */
        ctr_crypt_block(&k, (const uint8_t *)v->nonce, (uint32_t)v->n, buf, (size_t)v->len);
        int same = 1;
        for (int j = 0; j < v->len; j++) {
            same &= buf[j] == (uint8_t)(j * 31 + k_index);
        }
        check(same, "CTR round trip", i);
    }
    ctr_wipe(&k);
    if (failures) {
        printf("%d ctr test(s) failed\n", failures);
        return 1;
    }
    printf("ctr tests passed (%d CTR vectors)\n", n);
    return 0;
}
