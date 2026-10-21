#ifndef GUARD_H
#define GUARD_H

#include <stdint.h>

/*
 * The checks the V5F's machine-mode guards make, in plain C with no chip
 * headers, so unit_tests/guard can run them on the desktop:
 *
 *   - decoding a store that faulted in user mode, for the HardFault
 *     handler's store emulation (ch32h417_it.c): PMP entry 2 (pmp.c) makes
 *     0x40020000-0x40023FFF read-only to user mode, which fences DMA1,
 *     DMA2, DMAMUX, the flash controller and the RNG, but also RCC and
 *     CRC, which user mode writes. The handler does those writes for it.
 *   - where a Guarded DMA buffer may be (gdma.c).
 */

/* A store instruction, decoded */
typedef struct {
    uint32_t addr;                      /* effective address: rs1 + offset */
    uint32_t value;                     /* rs2, truncated to size */
    int size;                           /* 1, 2 or 4 bytes */
    int len;                            /* instruction length: 2 or 4 bytes */
} guard_store;

/**
 * Decodes the store instruction insn (its low 16 bits for a compressed
 * one; the rest is ignored then), with the faulting code's registers in
 * regs[0..31] (regs[0] must be 0). Knows SB, SH, SW, C.SW, C.SWSP and
 * WCH's XW stores (QK.C.SB, QK.C.SH, QK.C.SBSP, QK.C.SHSP, as LLVM's
 * RISCVInstrInfoXwch.td encodes them). Not FP stores or AMOs.
 * @return 0 and *s filled in, or -1 if insn isn't one of them.
 */
int guard_store_decode(uint32_t insn, const uint32_t regs[32], guard_store *s);

/**
 * Nonzero if the store s may be done for user mode: naturally aligned,
 * and wholly inside RCC (0x40021000-0x400213FF) or CRC
 * (0x40023000-0x400233FF), the registers in PMP entry 2's block that
 * user mode wrote before the entry covered them. Never DMA1, DMA2,
 * DMAMUX, the flash controller, USBFS or the RNG.
 */
int guard_store_allowed(const guard_store *s);

/* The emulated windows, for gdma.c's and ch32h417_it.c's static checks */
#define GUARD_BLOCK_BASE    0x40020000u     /* PMP entry 2: 16 KB */
#define GUARD_BLOCK_SIZE    0x4000u
#define GUARD_RCC_BASE      0x40021000u
#define GUARD_CRC_BASE      0x40023000u
#define GUARD_WINDOW_SIZE   0x400u

/**
 * Nonzero if the buffer [p, p + n) is one machine mode may copy to or
 * from for user mode: n > 0, p 4-byte aligned, all of it within
 * [lo, hi), and none of it in [xlo, xhi). gdma.c passes DTCM's .data and
 * .bss as [lo, hi) (so not the stack, where machine mode's own frames
 * are, nor code) and its own DMA buffer as [xlo, xhi).
 */
int guard_buffer_ok(uint32_t p, uint32_t n, uint32_t lo, uint32_t hi,
                    uint32_t xlo, uint32_t xhi);

#endif /* GUARD_H */
