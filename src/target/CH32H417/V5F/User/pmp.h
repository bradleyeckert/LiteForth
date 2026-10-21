#ifndef PMP_H
#define PMP_H

#include <stdint.h>

/*
 * The V5F's PMP (doc/admin/firmware-updates.md section 8) and the machine-mode
 * services behind it. LiteForth runs in user mode (WCH's start-up code
 * `mret`s into `main` with MPP = 0), where the PMP CSRs can't be reached.
 *
 *   #  region                     user   machine  locked
 *   0  0x00000000..0x08013FFF     none   none     yes  TOR: all of the code
 *                                                      flash's low window, and
 *                                                      in the high window the
 *                                                      V3F's image (stage 0
 *                                                      and 1) and stage 1's
 *                                                      records A and B (the
 *                                                      device token, and the
 *                                                      board's key)
 *   1  0x20100000..0x2013FFFF     none   none     yes  the V3F's RAM: stage 1's
 *                                                      code, data and stack
 *                                                      (the shared block, from
 *                                                      0x20178000, stays open)
 *   2  0x40020000..0x40023FFF     R      all      no   DMA1, DMA2, DMAMUX,
 *                                                      RCC, the flash
 *                                                      controller, CRC, USBFS
 *                                                      and the RNG
 *   3  all memory                 RWX    RWX      yes
 *
 * A locked entry binds machine mode too, and only a reset clears it. An
 * unlocked one binds only user mode, so the machine-mode services below
 * can still program flash. They're reached by
 * `ecall` (Ecall_U_Mode_Handler) and check every address they're given.
 * A service never writes to memory user mode points it at unless
 * guard_buffer_ok allows it (.data and .bss, not the stack): the handler's
 * own frames are on user mode's stack, just below the caller's sp, so such
 * a write could change where machine mode returns to; gdma.c checks with
 * guard_buffer_ok. Services: programming LiteForth's flash pages here (it
 * only reads its source, which must lie in DTCM), and Guarded DMA
 * (gdma.c). The V5F holds no key: stage 1 does. All of this runs from
 * flash, where user mode can't write it (mmode.h). The V5F reads
 * the code flash's low window only in its start-up code, at a cold start,
 * before the lock; it's never restarted without a chip reset (Ctrl+X x3
 * resets the chip, Common/cdc_bridge.c), so that code never runs again
 * with entry 0 locked.
 *
 * Entry 2 is about DMA: a DMA controller isn't bound by the PMP and can
 * read or write any memory, so user mode mustn't program one. DMA1, DMA2
 * and DMAMUX share one 16 KB NAPOT region with the flash controller (and
 * RCC, CRC, USBFS and the RNG), so one entry fences them all. It's
 * read-only to user mode, not closed, so reading any of them (RCC's clock
 * settings, a DMA channel's flags) still works. Writes are another matter:
 *   - DMA: only through Guarded DMA (gdma.h), the machine-mode service
 *     that owns the channels, their dedicated buffer, and burning both
 *     after use;
 *   - RCC and CRC, which user mode writes (WCH's drivers enable clocks,
 *     lftime.c runs the RTC's domain, Forth's I/O page reaches CRC): the
 *     HardFault handler (ch32h417_it.c) does the store for it, if
 *     guard_store_allowed says it's one of theirs, and carries on after
 *     it;
 *   - the flash controller: only through the flash service below;
 *   - USBFS (the V3F's) and the RNG: not at all. Any other store there faults: to Forth with ior -127
 *     (ERR_BUS_FAULT) if it came from Forth, else it resets the chip.
 */

/**
 * Sets and locks the entries above. Called by the start-up code
 * (startup_ch32h417_v5f.S) in machine mode, just before its `mret` into
 * `main`. Then tries to clear entry 0 and keeps what the CSRs read back,
 * for pmp_report. Touches no memory but its own variables and the stack.
 */
void pmp_lock(void);

/**
 * Prints on the debug port what pmp_lock set and read back:
 * `V5F PMP: cfg0 ... addr ... ...`, and whether the entries stayed locked
 * when it tried to clear entry 0.
 */
void pmp_report(void);

/* Services 8 to 15 are Guarded DMA's (gdma_serve, GDMA_SVC_*). 16 to 18
   were board.c's, which held the board's key before stage 1 did. */
enum { PMP_SVC_FLASH_PROGRAM = 1 };

/**
 * Calls machine-mode service n with arguments a and b, by `ecall`
 * (Ecall_U_Mode_Handler). From user mode only.
 * @return The service's ior: 0, or ERR_UNSUPPORTED_OPERATION for an
 * unknown n, or what the service returns.
 */
int pmp_call(uint32_t n, uint32_t a, uint32_t b);

/**
 * pmp_call with a third argument, c (the Guarded DMA services, gdma.h,
 * which are 8 to 15).
 */
int pmp_call3(uint32_t n, uint32_t a, uint32_t b, uint32_t c);

/**
 * Nonzero if [p, p + n) lies in DTCM: where a service's buffers must be,
 * so it can't be made to read or write anywhere else.
 */
int pmp_in_dtcm(uint32_t p, uint32_t n);

/**
 * Erases one LiteForth flash page at addr (__lf_flash_start plus a whole
 * number of pages, Link_v5f.ld) and programs it from src, in machine mode,
 * then checks it. src must lie in DTCM.
 * @return 0, ERR_INVALID_ADDRESS if addr or src isn't allowed, or
 * ERR_FLASH_WRITE_SECTOR if the flash didn't take it.
 */
int pmp_flash_program(uint32_t addr, const uint32_t *src);

#endif /* PMP_H */
