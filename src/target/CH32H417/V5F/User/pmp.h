#ifndef PMP_H
#define PMP_H

#include <stdint.h>

/*
 * The V5F's PMP (doc/admin/firmware-updates.md section 8) and the machine-mode
 * services behind it. LiteForth runs in user mode (WCH's start-up code
 * `mret`s into `main` with MPP = 0), where the PMP CSRs can't be reached.
 *
 *   #  region                     user   machine  locked
 *   0  0x00070000..0x00077FFF     none   none     yes  the code flash's low
 *                                                      window over the records
 *   1  0x08070000..0x08077FFF     none   all      no   the board record, and
 *                                                      stage 1's records A and
 *                                                      B (the device token)
 *   2  0x40022000..0x40022FFF     none   all      no   the flash controller
 *   3  all memory                 RWX    RWX      yes
 *
 * A locked entry binds machine mode too, and only a reset clears it. An
 * unlocked one binds only user mode, so the machine-mode services below
 * can still program flash and reach the board record. They're reached by
 * `ecall` (Ecall_U_Mode_Handler) and check every address they're given:
 * programming LiteForth's flash pages here, and board.c's board_serve,
 * which keeps the board's private key in machine mode; buffers in DTCM. The V5F reads the code flash's low window only in its start-up
 * copy, before the lock. PMP doesn't stop DMA.
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

/* Services 16 and up are board.c's (board_serve, BOARD_SVC_*) */
enum { PMP_SVC_FLASH_PROGRAM = 1 };

/**
 * Calls machine-mode service n with arguments a and b, by `ecall`
 * (Ecall_U_Mode_Handler). From user mode only.
 * @return The service's ior: 0, or ERR_UNSUPPORTED_OPERATION for an
 * unknown n, or what the service returns.
 */
int pmp_call(uint32_t n, uint32_t a, uint32_t b);

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
