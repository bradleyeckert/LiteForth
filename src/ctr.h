#ifndef CTR_H
#define CTR_H

#include <stdint.h>
#include <stddef.h>

/*
 * AES-128 and SM4 in software, and CTR mode as sealed block files use it
 * (doc/encrypted-blocks.md): the 16-byte counter block for the first 16
 * bytes of block n is an 8-byte nonce followed by n * 256 as a 64-bit
 * big-endian number, counting up by one for each 16 bytes. On the
 * CH32H417 (CTR_HW, defined by its lf_ctr.c), ctr_crypt_block hands whole
 * blocks to the ECDC engine (ctr_hw_crypt, V5F/User/ecdc.c).
 */

enum { CTR_AES128 = 1, CTR_SM4 = 2 };   /* the cipher byte of block 0 */

typedef struct {
    int cipher;                         /* CTR_AES128 or CTR_SM4 */
    uint32_t rk[44];                    /* round keys: AES uses 44, SM4 32 */
    uint8_t key[16];                    /* the key itself, for an engine */
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
 * at counter nonce || n * 256. len need not be a multiple of 16. With
 * CTR_HW, a whole number of 16-byte units at a 4-byte aligned buf goes to
 * ctr_hw_crypt first, and to software only if that declines.
 */
void ctr_crypt_block(const ctr_key *k, const uint8_t nonce[8], uint32_t n,
                     uint8_t *buf, size_t len);

/**
 * ctr_crypt_block in software only, whatever CTR_HW says: for tests that
 * compare an engine with it.
 */
void ctr_crypt_block_sw(const ctr_key *k, const uint8_t nonce[8], uint32_t n,
                        uint8_t *buf, size_t len);

#ifdef CTR_HW
/**
 * Supplied by the target when CTR_HW is defined: encrypts or decrypts len
 * bytes (a multiple of 16) at buf (4-byte aligned) in place in CTR mode
 * with k's key, from the 16-byte counter block counter, stepping its low
 * 64 bits by one per 16 bytes.
 * Returns 0 if it did, or nonzero to leave it to software.
 */
int ctr_hw_crypt(const ctr_key *k, const uint8_t counter[16], uint8_t *buf, size_t len);
#endif

/**
 * Erases an expanded key.
 */
void ctr_wipe(ctr_key *k);

#endif /* CTR_H */
