/* Tests src/blake2s.c against Python's hashlib.blake2s (vectors.h): plain
   hashes, keys of 1 to 32 bytes, salt, personalization and short digests,
   fed whole and in pieces, plus the parameter checks. The sealed block
   format (doc/encrypted-blocks.md) uses all of these. */
#include "blake2s.h"
#include <stdio.h>
#include <string.h>
#include "vectors.h"

static int failures;

static void check(int ok, const char *what, int i) {
    if (!ok) {
        printf("FAIL: %s (vector %d)\n", what, i);
        failures++;
    }
}

/* hash vector v's message (bytes 0, 1, 2, ...) in pieces of `step` bytes */
static void hash_vector(const b2s_vector *v, int step, uint8_t *out) {
    uint8_t msg[4096];
    blake2s_state S;
    for (int j = 0; j < v->len; j++) msg[j] = (uint8_t)j;
    b2s_init(&S, v->hsize, v->keylen ? (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07"
             "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17"
             "\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" : NULL, v->keylen,
             (const uint8_t *)v->salt, (const uint8_t *)v->personal);
    for (int j = 0; j < v->len; j += step) {
        int n = v->len - j < step ? v->len - j : step;
        b2s_puts(&S, msg + j, n);
    }
    b2s_final(&S, out);
}

static void test_vectors(void) {
    int n = (int)(sizeof b2s_vectors / sizeof b2s_vectors[0]);
    static const int steps[] = { 1, 7, 64, 4096 };
    for (int i = 0; i < n; i++) {
        for (unsigned s = 0; s < sizeof steps / sizeof steps[0]; s++) {
            uint8_t out[32];
            hash_vector(&b2s_vectors[i], steps[s], out);
            check(memcmp(out, b2s_vectors[i].hash, b2s_vectors[i].hsize) == 0,
                  "hash differs from hashlib", i);
        }
    }
    printf("%d hashlib vectors\n", n);
}

static void test_params(void) {
    blake2s_state S;
    uint8_t key[33] = {0}, out[32];
    check(b2s_init(&S, 0, NULL, 0, NULL, NULL) == 0, "hsize 0 accepted", 0);
    check(b2s_init(&S, 33, NULL, 0, NULL, NULL) == 0, "hsize 33 accepted", 0);
    check(b2s_init(&S, 32, key, 33, NULL, NULL) == 0, "keylen 33 accepted", 0);
    check(b2s_init(&S, 32, NULL, 16, NULL, NULL) == 0, "NULL key accepted", 0);
    check(b2s_init(&S, 16, NULL, 0, NULL, NULL) == 16, "hsize 16 refused", 0);
    b2s_final(&S, out);
    check(b2s_final(&S, out) == -1, "second final accepted", 0);
}

int main(void) {
    test_vectors();
    test_params();
    if (failures) {
        printf("%d blake2s test(s) failed\n", failures);
        return 1;
    }
    puts("blake2s tests passed");
    return 0;
}
