/*
 * The V5F's PMP and its machine-mode services: see pmp.h.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "options.h"                    /* FLASH_PAGE_CELLS */
#include "pmp.h"
#include "../../../../errcodes.h"

/* pmpcfg bytes: L (locked), A = NAPOT, X W R */
#define PMP_L       0x80u
#define PMP_NAPOT   0x18u
#define PMP_RWX     0x07u

/* pmpaddr for a NAPOT region: base aligned to size, size a power of 2 >= 8 */
#define NAPOT(base, size)   (((uint32_t)(base) >> 2) | (((uint32_t)(size) >> 3) - 1u))

#define RECORDS     0x08070000u                 /* the board record, then stage 1's */
#define FLASH_REGS  0x40022000u                 /* FLASH_R_BASE */

#define PMP_ADDR0   NAPOT(0x00070000u, 0x8000u)
#define PMP_ADDR1   NAPOT(RECORDS, 0x8000u)
#define PMP_ADDR2   NAPOT(FLASH_REGS, 0x1000u)
#define PMP_ADDR3   0x1FFFFFFFu                 /* NAPOT: 4 GB from 0, all of memory */
/* (0xFFFFFFFF reads back as 0x3FFFFFFF: the core doesn't keep pmpaddr's
   top two bits, address bits 33:32. Both still cover everything.) */
#define PMP_CFG0    ((PMP_L | PMP_NAPOT) << 0 | (PMP_NAPOT) << 8 | \
                     (PMP_NAPOT) << 16 | (PMP_L | PMP_NAPOT | PMP_RWX) << 24)

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
#define RECORD_ADDR     ((uint32_t)__lf_flash_start + (uint32_t)__lf_flash_size)
#define RECORD_SECTOR   8192u
#define RECORD_BYTES    256u
#define DTCM_END        (DTCM_BASE + 0x40000u)

enum { SVC_FLASH_PROGRAM = 1, SVC_RECORD_WRITE, SVC_RECORD_READ };

/* The call: written in user mode, read and answered by the handler. The
   handler can't return a value in a0, because the hardware (HPE) puts the
   caller's registers back at its mret. */
static volatile struct {
    uint32_t n, a, b;
    int32_t ior;
} svc;

/* Nonzero if [p, p + n) lies in DTCM */
static int in_dtcm(uint32_t p, uint32_t n)
{
    return p >= DTCM_BASE && p < DTCM_END && n <= DTCM_END - p;
}

/* In machine mode. Nothing here may fault: a fault in a trap handler
   locks the core up (ch32h417-notes.md). Every address is checked first,
   and the flash it touches is in entry 1 or LiteForth's pages, which
   machine mode reaches. */
static int32_t serve(uint32_t n, uint32_t a, uint32_t b)
{
    uint32_t start = (uint32_t)__lf_flash_start;
    uint32_t size = (uint32_t)__lf_flash_size;
    switch (n) {
    case SVC_FLASH_PROGRAM:             /* a = page address, b = source */
        if (a < start || a - start >= size || (a - start) % PAGE_BYTES
            || !in_dtcm(b, PAGE_BYTES) || (b & 3u)) {
            return ERR_INVALID_ADDRESS;
        }
        if (FLASH_ROM_ERASE(a, PAGE_BYTES) != FLASH_COMPLETE
            || FLASH_ROM_WRITE(a, (uint32_t *)b, PAGE_BYTES) != FLASH_COMPLETE
            || memcmp((const void *)a, (const void *)b, PAGE_BYTES) != 0) {
            return ERR_FLASH_WRITE_SECTOR;
        }
        return 0;
    case SVC_RECORD_WRITE:              /* a = source */
        if (!in_dtcm(a, RECORD_BYTES) || (a & 3u)) {
            return ERR_INVALID_ADDRESS;
        }
        if (FLASH_ROM_ERASE(RECORD_ADDR, RECORD_SECTOR) != FLASH_COMPLETE
            || FLASH_ROM_WRITE(RECORD_ADDR, (uint32_t *)a, RECORD_BYTES) != FLASH_COMPLETE
            || memcmp((const void *)RECORD_ADDR, (const void *)a, RECORD_BYTES) != 0) {
            return ERR_FLASH_WRITE_SECTOR;
        }
        return 0;
    case SVC_RECORD_READ:               /* a = destination */
        if (!in_dtcm(a, RECORD_BYTES)) {
            return ERR_INVALID_ADDRESS;
        }
        memcpy((void *)a, (const void *)RECORD_ADDR, RECORD_BYTES);
        return 0;
    default:
        return ERR_UNSUPPORTED_OPERATION;
    }
}

void Ecall_U_Mode_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void Ecall_U_Mode_Handler(void)
{
    uint32_t pc;
    __asm volatile ("csrr %0, mepc" : "=r"(pc));
    __asm volatile ("csrw mepc, %0" : : "r"(pc + 4));   /* past the ecall */
    uint32_t n = svc.n, a = svc.a, b = svc.b;           /* copied, then checked */
    svc.ior = serve(n, a, b);
}

static int call(uint32_t n, uint32_t a, uint32_t b)
{
    svc.n = n;
    svc.a = a;
    svc.b = b;
    svc.ior = ERR_UNSUPPORTED_OPERATION;
    __asm volatile ("ecall" : : : "memory");
    return svc.ior;
}

int pmp_flash_program(uint32_t addr, const uint32_t *src)
{
    return call(SVC_FLASH_PROGRAM, addr, (uint32_t)src);
}

int pmp_record_write(const void *src)
{
    return call(SVC_RECORD_WRITE, (uint32_t)src, 0);
}

int pmp_record_read(void *dst)
{
    return call(SVC_RECORD_READ, (uint32_t)dst, 0);
}
