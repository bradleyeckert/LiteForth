/*
 * The V5F's PMP and its machine-mode services: see pmp.h.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "options.h"                    /* FLASH_PAGE_CELLS */
#include "pmp.h"
#include "board.h"                      /* board_serve */
#include "gdma.h"                       /* gdma_serve */
#include "guard.h"                      /* GUARD_BLOCK_BASE, GUARD_BLOCK_SIZE */
#include "../../../../errcodes.h"

/* pmpcfg bytes: L (locked), A = TOR or NAPOT, X W R */
#define PMP_L       0x80u
#define PMP_TOR     0x08u
#define PMP_NAPOT   0x18u
#define PMP_RWX     0x07u
#define PMP_R       0x01u

/* pmpaddr for a NAPOT region: base aligned to size, size a power of 2 >= 8 */
#define NAPOT(base, size)   (((uint32_t)(base) >> 2) | (((uint32_t)(size) >> 3) - 1u))

#define RECORDS     0x08070000u                 /* the board record, then stage 1's */

/* Entry 2: 16 KB from 0x40020000, the one NAPOT region that holds both the
   DMA registers and the flash controller (pmp.h) */
_Static_assert(GUARD_BLOCK_BASE == DMA1_BASE, "entry 2 starts at DMA1");
_Static_assert(GUARD_BLOCK_BASE % GUARD_BLOCK_SIZE == 0, "a NAPOT region is aligned to its size");
_Static_assert(FLASH_R_BASE >= GUARD_BLOCK_BASE
               && FLASH_R_BASE + 0x400u <= GUARD_BLOCK_BASE + GUARD_BLOCK_SIZE,
               "entry 2 covers the flash controller");
_Static_assert(RNG_BASE >= GUARD_BLOCK_BASE
               && RNG_BASE + 0x400u <= GUARD_BLOCK_BASE + GUARD_BLOCK_SIZE,
               "entry 2 covers the RNG");

_Static_assert(PMP_SVC_FLASH_PROGRAM < GDMA_SVC_START && GDMA_SVC_LAST < BOARD_SVC_INFO,
               "the services' numbers don't overlap");

/* Entry 0, TOR: everything below the V5F's slot in the high window. That's
   all of the code flash's low window (the V3F's image, the V5F's, LiteForth's
   pages and the records, all seen at 0x00000000 too), and the V3F's image
   (stage 0 and 1) in the high window. Locked, so machine mode can't reach it
   either. The V5F runs from the low window only at a cold start, before
   this (its start-up code), and nothing restarts it without a chip reset:
   Ctrl+X x3 resets the chip (Common/cdc_bridge.c). */
#define V5F_SLOT_HIGH   0x08010000u     /* Link_v5f.ld's FLASH, in the high window */
#define PMP_ADDR0   (V5F_SLOT_HIGH >> 2)
#define PMP_ADDR1   NAPOT(RECORDS, 0x8000u)
#define PMP_ADDR2   NAPOT(GUARD_BLOCK_BASE, GUARD_BLOCK_SIZE)
#define PMP_ADDR3   0x1FFFFFFFu                 /* NAPOT: 4 GB from 0, all of memory */
/* (0xFFFFFFFF reads back as 0x3FFFFFFF: the core doesn't keep pmpaddr's
   top two bits, address bits 33:32. Both still cover everything.) */
#define PMP_CFG0    ((PMP_L | PMP_TOR) << 0 | (PMP_NAPOT) << 8 | \
                     (PMP_NAPOT | PMP_R) << 16 | (PMP_L | PMP_NAPOT | PMP_RWX) << 24)

static uint32_t got_cfg, got_addr[4];   /* read back after the lock */
static uint32_t kept_cfg, kept_addr0;   /* after trying to clear entry 0 */

void pmp_lock(void)
{
    __asm volatile ("csrw pmpaddr0, %0" : : "r"(PMP_ADDR0));
    __asm volatile ("csrw pmpaddr1, %0" : : "r"(PMP_ADDR1));
    __asm volatile ("csrw pmpaddr2, %0" : : "r"(PMP_ADDR2));
    __asm volatile ("csrw pmpaddr3, %0" : : "r"(PMP_ADDR3));
    __asm volatile ("csrw pmpcfg0, %0" : : "r"(PMP_CFG0));  /* all four at once */
    __asm volatile ("csrr %0, pmpcfg0" : "=r"(got_cfg));
    __asm volatile ("csrr %0, pmpaddr0" : "=r"(got_addr[0]));
    __asm volatile ("csrr %0, pmpaddr1" : "=r"(got_addr[1]));
    __asm volatile ("csrr %0, pmpaddr2" : "=r"(got_addr[2]));
    __asm volatile ("csrr %0, pmpaddr3" : "=r"(got_addr[3]));
    __asm volatile ("csrw pmpaddr0, %0" : : "r"(0u));       /* locked: ignored? */
    __asm volatile ("csrc pmpcfg0, %0" : : "r"(0xFFu));
    __asm volatile ("csrr %0, pmpcfg0" : "=r"(kept_cfg));
    __asm volatile ("csrr %0, pmpaddr0" : "=r"(kept_addr0));
}

void pmp_report(void)
{
    int set = got_cfg == PMP_CFG0 && got_addr[0] == PMP_ADDR0 && got_addr[1] == PMP_ADDR1
              && got_addr[2] == PMP_ADDR2 && got_addr[3] == PMP_ADDR3;
    int locked = kept_cfg == got_cfg && kept_addr0 == got_addr[0];
    printf("V5F PMP: cfg0 %08lX, addr %08lX %08lX %08lX %08lX: %s, %s\r\n",
           (unsigned long)got_cfg, (unsigned long)got_addr[0], (unsigned long)got_addr[1],
           (unsigned long)got_addr[2], (unsigned long)got_addr[3],
           set ? "as set" : "NOT as set",
           locked ? "locked (clearing entry 0 was ignored)" : "NOT locked");
    if (!locked) {
        printf("V5F PMP: after clearing entry 0: cfg0 %08lX, addr0 %08lX\r\n",
               (unsigned long)kept_cfg, (unsigned long)kept_addr0);
    }
}

/* ---- The machine-mode services ------------------------------------------ */

extern const uint32_t __lf_flash_start[];   /* Link_v5f.ld */
extern const uint8_t  __lf_flash_size[];

#define PAGE_BYTES      (FLASH_PAGE_CELLS * 4u)
#define DTCM_END        (DTCM_BASE + 0x40000u)

/* The call: written in user mode, read and answered by the handler. The
   handler can't return a value in a0, because the hardware (HPE) puts the
   caller's registers back at its mret. */
static volatile struct {
    uint32_t n, a, b, c;
    int32_t ior;
} svc;

int pmp_in_dtcm(uint32_t p, uint32_t n)
{
    return p >= DTCM_BASE && p < DTCM_END && n <= DTCM_END - p;
}

/* In machine mode. Nothing here may fault: a fault in a trap handler
   locks the core up (ch32h417-notes.md). Every address is checked first,
   and the flash it touches is in entry 1 or LiteForth's pages, which
   machine mode reaches. */
static int32_t serve(uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t start = (uint32_t)__lf_flash_start;
    uint32_t size = (uint32_t)__lf_flash_size;
    if (n == PMP_SVC_FLASH_PROGRAM) {   /* a = page address, b = source */
        if (a < start || a - start >= size || (a - start) % PAGE_BYTES
            || !pmp_in_dtcm(b, PAGE_BYTES) || (b & 3u)) {
            return ERR_INVALID_ADDRESS;
        }
        if (FLASH_ROM_ERASE(a, PAGE_BYTES) != FLASH_COMPLETE
            || FLASH_ROM_WRITE(a, (uint32_t *)b, PAGE_BYTES) != FLASH_COMPLETE
            || memcmp((const void *)a, (const void *)b, PAGE_BYTES) != 0) {
            return ERR_FLASH_WRITE_SECTOR;
        }
        return 0;
    }
    if (n >= GDMA_SVC_START && n <= GDMA_SVC_LAST) {
        return gdma_serve(n, a, b, c);  /* Guarded DMA */
    }
    if (n >= BOARD_SVC_INFO && n <= BOARD_SVC_LAST) {
        return board_serve(n, a, b);    /* the board record and its key */
    }
    return ERR_UNSUPPORTED_OPERATION;
}

void Ecall_U_Mode_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void Ecall_U_Mode_Handler(void)
{
    uint32_t pc;
    __asm volatile ("csrr %0, mepc" : "=r"(pc));
    __asm volatile ("csrw mepc, %0" : : "r"(pc + 4));   /* past the ecall */
    uint32_t n = svc.n, a = svc.a, b = svc.b, c = svc.c; /* copied, then checked */
    svc.ior = serve(n, a, b, c);
}

int pmp_call3(uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{
    svc.n = n;
    svc.a = a;
    svc.b = b;
    svc.c = c;
    svc.ior = ERR_UNSUPPORTED_OPERATION;
    __asm volatile ("ecall" : : : "memory");
    return svc.ior;
}

int pmp_call(uint32_t n, uint32_t a, uint32_t b)
{
    return pmp_call3(n, a, b, 0);
}

int pmp_flash_program(uint32_t addr, const uint32_t *src)
{
    return pmp_call(PMP_SVC_FLASH_PROGRAM, addr, (uint32_t)src);
}
