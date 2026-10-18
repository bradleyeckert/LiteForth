#ifndef ECDC_H
#define ECDC_H

#include <stdint.h>

/*
 * The CH32H417's AES/SM4 engine (ECDC) in CTR mode, driven through its
 * registers as doc/ch32h417-notes.md describes them (WCH's driver has the
 * CTR/ECB bit the wrong way round and its interrupt constants truncated).
 * Each call resets the engine, loads the key and the first counter block,
 * expands the key, then runs the engine's RAM-to-RAM mode over the data.
 */

enum { ECDC_AES = 1, ECDC_SM4 = 2 };    /* as CTR_AES128 and CTR_SM4 (ctr.h) */

/**
 * Encrypts or decrypts (the same in CTR mode) units 16-byte units from src
 * into dst with the engine: cipher ECDC_AES or ECDC_SM4, a 128-bit key,
 * and the 16-byte counter block for the first unit. src and dst must be
 * 4-byte aligned, may be the same, and must be RAM the engine's DMA can
 * reach. div is the engine clock's divider of PLLCLK (1 to 7).
 * Whether the engine steps the counter from one unit to the next is what
 * `ecdc-test` finds out.
 * Returns 0, or ERR_BLK_TIMEOUT if the key expansion or the transfer
 * didn't finish.
 */
int ecdc_ctr(int cipher, const uint8_t key[16], const uint8_t counter[16],
             const void *src, void *dst, uint32_t units, int div);

/**
 * ecdc_ctr with the byte order chosen: big nonzero sets DAT_MOD (what
 * ecdc_ctr uses), zero leaves it clear. For `ecdc-test`.
 */
int ecdc_ctr_order(int cipher, const uint8_t key[16], const uint8_t counter[16],
                   const void *src, void *dst, uint32_t units, int div, int big);

/**
 * Forth word `ecdc-test`  ( -- )
 * Runs a 4 KB block through the engine (AES in both byte orders and in
 * place, SM4) and through the software CTR of ctr.c, with
 * the sealed files' counter (ctr.h), and prints how many 16-byte units
 * match and how long each took. If none match, it says whether the
 * engine kept one counter for every unit or stepped it another way, and
 * prints units 0 and 1 (input, output, software keystream) in hex.
 * Returns 0.
 */
int lfAPI_ecdcTest(void);

#endif /* ECDC_H */
