/*
 * The V5F's PMP: see pmp.h.
 */
#include <stdio.h>
#include "pmp.h"

/* pmpcfg bytes: L (locked), A = NAPOT, X W R */
#define PMP_L       0x80u
#define PMP_NAPOT   0x18u
#define PMP_RWX     0x07u

/* pmpaddr for a NAPOT region: base aligned to size, size a power of 2 >= 8 */
#define NAPOT(base, size)   (((uint32_t)(base) >> 2) | (((uint32_t)(size) >> 3) - 1u))

#define PMP_ADDR0   NAPOT(0x00070000u, 0x8000u)
#define PMP_ADDR1   NAPOT(0x08072000u, 0x2000u)
#define PMP_ADDR2   NAPOT(0x08074000u, 0x2000u)
#define PMP_ADDR3   0xFFFFFFFFu                 /* NAPOT: all of memory */
#define PMP_CFG0    ((PMP_L | PMP_NAPOT) << 0 | (PMP_L | PMP_NAPOT) << 8 | \
                     (PMP_L | PMP_NAPOT) << 16 | (PMP_L | PMP_NAPOT | PMP_RWX) << 24)

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
