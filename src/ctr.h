#ifndef CTR_H
#define CTR_H

#include <stdint.h>
#include <stddef.h>

/*
 * AES-128 and SM4 in software, and CTR mode as sealed block files use it
 * (doc/encrypted-blocks.md): the 16-byte counter block for the first 16
 * bytes of block n is an 8-byte nonce followed by n * 256 as a 64-bit
 * big-endian number, counting up by one for each 16 bytes. The CH32H417
 * can do the same on its ECDC engine.
 */

enum { CTR_AES128 = 1, CTR_SM4 = 2 };   /* the cipher byte of block 0 */

typedef struct {
    int cipher;                         /* CTR_AES128 or CTR_SM4 */
    uint32_t rk[44];                    /* round keys: AES uses 44, SM4 32 */
} ctr_key;

/**
 * Expands a 128-bit key for cipher (CTR_AES128 or CTR_SM4) into k.
 * Returns 0, or -1 for an unknown cipher.
 */
int ctr_setkey(ctr_key *k, int cipher, const uint8_t key[16]);

/**
 * Encrypts one 16-byte block with k (ECB, for CTR and for tests).
 * in and out may be the same.
 */
void ctr_encrypt_block(const ctr_key *k, const uint8_t in[16], uint8_t out[16]);

/**
 * Encrypts or decrypts (the same operation in CTR mode) len bytes of
 * block n in place, starting at byte 0 of the block: the keystream starts
 * at counter nonce || n * 256. len need not be a multiple of 16.
 */
void ctr_crypt_block(const ctr_key *k, const uint8_t nonce[8], uint32_t n,
                     uint8_t *buf, size_t len);

/**
 * Erases an expanded key.
 */
void ctr_wipe(ctr_key *k);

#endif /* CTR_H */
