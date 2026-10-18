#ifndef ECDC_H
#define ECDC_H

#include <stdint.h>

/*
 * The CH32H417's AES/SM4 engine (ECDC) in CTR mode, driven through its
 * registers as doc/ch32h417-notes.md describes them (WCH's driver has the
 * CTR/ECB bit the wrong way round and its interrupt constants truncated).
 * ctr.c's ctr_hw_crypt (ctr.h, CTR_HW) is here too, so sealed block files
 * are decrypted by the engine, after a check against software.
 */

enum { ECDC_AES = 1, ECDC_SM4 = 2 };    /* as CTR_AES128 and CTR_SM4 (ctr.h) */

/**
 * Encrypts or decrypts (the same in CTR mode) units 16-byte units (1 to
 * 256) from src into dst with the engine: cipher ECDC_AES or ECDC_SM4, a
 * 128-bit key, and the 16-byte counter block for the first unit, which
 * the engine steps by one per unit. Resets the engine, loads the key and
 * counter, expands the key and waits a little past its flag, then runs
 * RAM-to-RAM at the undivided clock. src and dst must be 32-byte aligned
 * RAM the engine's DMA reaches (the V5F's DTCM does), and may be the same:
 * 4-byte aligned buffers failed.
 * Returns 0, or ERR_BLK_TIMEOUT if the key expansion or the transfer
 * didn't finish.
 */
int ecdc_ctr(int cipher, const uint8_t key[16], const uint8_t counter[16],
             const void *src, void *dst, uint32_t units);

/**
 * Forth word `ecdc-test`  ( -- )
 * Runs a 4 KB block through the engine (AES out of place and in place,
 * AES without the pause after key expansion, SM4, and both through
 * ctr_crypt_block, the path sealed files take) and through the software
 * CTR (ctr_crypt_block_sw), and prints how many 16-byte units match, the
 * wrong words if any, and the times.
 * Returns 0.
 */
int lfAPI_ecdcTest(void);

#endif /* ECDC_H */
