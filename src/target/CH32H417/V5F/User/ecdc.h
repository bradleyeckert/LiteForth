#ifndef ECDC_H
#define ECDC_H

#include <stdint.h>

/*
 * The CH32H417's AES/SM4 engine (ECDC) in CTR mode, driven through its
 * registers as doc/ch32h417-notes.md describes them (WCH's driver has the
 * CTR/ECB bit the wrong way round and its interrupt constants truncated).
 * ctr.c's ctr_hw_crypt (ctr.h, CTR_HW) is here too, so packages are
 * decrypted by the engine, after a check against software.
 *
 * The engine is a RAM-to-RAM bus master, which the PMP can't bind, so under
 * the clock fence (SYS-SEC-13, doc/admin/firmware-updates.md section 8 part
 * 2) user mode may not run it: the store guard refuses any write that would
 * turn its clock on (guard_rcc_turns_on_master), and start-up leaves the
 * clock off. The engine is driven entirely in machine mode, by an ecall
 * service (PMP_SVC_ECDC, ecdc_serve), which points it only at its own DTCM
 * buffer, copies to and from a user buffer it has checked, and turns the
 * clock off again -- the same shape as Guarded DMA (gdma.h).
 */

enum { ECDC_AES = 1, ECDC_SM4 = 2 };    /* as CTR_AES128 and CTR_SM4 (ctr.h) */

#define ECDC_BUF_BYTES  4096u           /* the engine's buffer: 256 16-byte units */

/* The ecall request (pmp.h, PMP_SVC_ECDC): user mode fills it, machine mode
   reads it. It must lie in RAM the service may read (pmp_in_ram). buf is the
   in-and-out data, a buffer guard_buffer_ok allows, a whole number of
   16-byte units, bytes long (16 to ECDC_BUF_BYTES). */
typedef struct {
    uint8_t  key[16];
    uint8_t  counter[16];               /* the counter block for the first unit */
    uint32_t cipher;                    /* ECDC_AES or ECDC_SM4 */
    uint32_t buf;                       /* the data, encrypted or decrypted in place */
    uint32_t bytes;
} ecdc_req;

/**
 * MACHINE MODE ONLY: pmp.c's ecall handler calls this for PMP_SVC_ECDC.
 * reqp is a user-mode pointer to an ecdc_req. Checks it and the buffer,
 * copies the buffer into the engine's own buffer, runs the engine in place
 * there, copies the result back (only if the whole run succeeded, so a
 * failure leaves the user buffer for software to redo), zeroes the engine
 * buffer and drops the clock. Nothing in it may fault (a fault in a trap
 * handler locks the core up): every address is checked first.
 * @return 0; ERR_INVALID_ADDRESS for a bad request or buffer;
 * ERR_INVALID_NUMERIC for a bad cipher or size; ERR_BLK_TIMEOUT if the
 * engine didn't finish.
 */
int32_t ecdc_serve(uint32_t reqp);

/**
 * Forth word `ecdc-test`  ( -- )
 * Runs a 4 KB block through the engine (via the service) and through the
 * software CTR for AES and SM4, and through ctr_crypt_block (the path
 * packages take), and prints how many 16-byte units match, the wrong words
 * if any, the times, and whether ctr_crypt_block uses the engine.
 * Returns 0.
 */
int lfAPI_ecdcTest(void);

#endif /* ECDC_H */
