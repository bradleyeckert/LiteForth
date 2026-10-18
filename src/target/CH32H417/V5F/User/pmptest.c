/*
 * pmp-test (pmptest.h): the V5F's PMP and what it can reach.
 *
 * pmp_test_m runs in machine mode, inside Ecall_U_Mode_Handler (an ecall
 * from lfAPI_pmpTest: vector 8, which WCH's start-up code otherwise points
 * at a weak handler that loops forever), with interrupts off. It points mtvec at pmp_trap so an
 * access fault is counted instead of resetting the chip, and puts mtvec,
 * mepc and mstatus back at the end (a trap inside it overwrites them).
 *
 * The probes are in assembly so their instructions are known: each load or
 * store is one 4-byte instruction (no RVC), so pmp_trap skips a faulting
 * one by adding 4 to mepc; a fault fetching an instruction (pmp_exec)
 * returns to the caller through ra. The _u probes set mstatus.MPRV with
 * MPP = user for just their one load or store, so it's checked as a
 * user-mode access, while the C code around them stays in machine mode.
 *
 * Output goes into a buffer that lfAPI_pmpTest prints after the ecall.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "pmptest.h"
#include "../../../../tools.h"          /* lf_puts, lfCR */

volatile uint32_t pmp_traps;            /* traps taken */
volatile uint32_t pmp_cause;            /* the last one's mcause */
volatile uint32_t pmp_tval;             /* and mtval */

uint32_t pmp_load(uintptr_t a);         /* *a, or a if it faulted */
void pmp_store(uintptr_t a, uint32_t v);
uint32_t pmp_load_u(uintptr_t a);       /* the same, as user mode */
void pmp_store_u(uintptr_t a, uint32_t v);
void pmp_exec(uintptr_t a);             /* calls a */
void pmp_trap(void);

__asm__(
"   .section .text.pmp_probes,\"ax\",@progbits\n"
"   .option push\n"
"   .option norvc\n"
"   .align 2\n"
"   .global pmp_load\n"
"pmp_load:\n"
"   mv   a1, a0\n"
"   lw   a0, 0(a1)\n"
"   ret\n"
"   .global pmp_store\n"
"pmp_store:\n"
"   sw   a1, 0(a0)\n"
"   ret\n"
"   .global pmp_load_u\n"
"pmp_load_u:\n"
"   li   t0, 0x20000\n"             /* MPRV */
"   li   t1, 0x1800\n"              /* MPP */
"   csrc mstatus, t1\n"             /* MPP = user */
"   csrs mstatus, t0\n"
"   mv   a1, a0\n"
"   lw   a0, 0(a1)\n"
"   csrc mstatus, t0\n"
"   ret\n"
"   .global pmp_store_u\n"
"pmp_store_u:\n"
"   li   t0, 0x20000\n"
"   li   t1, 0x1800\n"
"   csrc mstatus, t1\n"
"   csrs mstatus, t0\n"
"   sw   a1, 0(a0)\n"
"   csrc mstatus, t0\n"
"   ret\n"
"   .global pmp_exec\n"
"pmp_exec:\n"
"   jr   a0\n"                      /* returns to our caller through ra */
"   .align 6\n"                     /* mtvec's base */
"   .global pmp_trap\n"
"pmp_trap:\n"
"   addi sp, sp, -16\n"
"   sw   t0, 0(sp)\n"
"   sw   t1, 4(sp)\n"
"   la   t1, pmp_traps\n"
"   lw   t0, 0(t1)\n"
"   addi t0, t0, 1\n"
"   sw   t0, 0(t1)\n"
"   csrr t0, mcause\n"
"   la   t1, pmp_cause\n"
"   sw   t0, 0(t1)\n"
"   csrr t1, mtval\n"
"   la   t0, pmp_tval\n"
"   sw   t1, 0(t0)\n"
"   csrr t0, mcause\n"
"   li   t1, 1\n"                   /* instruction access fault */
"   beq  t0, t1, 1f\n"
"   csrr t0, mepc\n"
"   addi t0, t0, 4\n"               /* skip the probe's lw or sw */
"   csrw mepc, t0\n"
"   j    2f\n"
"1: csrw mepc, ra\n"                /* back to whoever called pmp_exec */
"2: li   t0, 0x1800\n"
"   csrs mstatus, t0\n"             /* mret back to machine mode */
"   lw   t0, 0(sp)\n"
"   lw   t1, 4(sp)\n"
"   addi sp, sp, 16\n"
"   mret\n"
"   .option pop\n"
"   .text\n");

static uint32_t target[8] __attribute__((aligned(32)));    /* entry 2's region */
static uint32_t elsewhere = 0x5A5A5A5Au;                    /* a user load outside it */

#define CSR_READ(name) ({ uint32_t v_; __asm volatile ("csrr %0, " #name : "=r"(v_)); v_; })
#define CSR_WRITE(name, v) __asm volatile ("csrw " #name ", %0" :: "r"(v) : "memory")

/* ------------------------------------------------------------------------
 * Progress marks straight to the debug UART (USART1, polled, as
 * HardFault_Handler writes it), so a hang shows where it stopped
 */
static void mark(const char *s) {
    while (*s) {
        while ((USART1->STATR & USART_FLAG_TXE) == 0) {
        }
        USART1->DATAR = (uint8_t)*s++;
    }
}

/* ------------------------------------------------------------------------
 * Output, kept until lfAPI_pmpTest prints it
 */
static char out[2048];
static size_t out_len;

static void say(const char *s) {
    for (const char *p = s; *p; p++) {                  /* the debug UART too, at once */
        mark(*p == '\n' ? "\r\n" : (char[2]){ *p, 0 });
    }
    size_t n = strlen(s);
    if (out_len + n < sizeof out) {
        memcpy(out + out_len, s, n);
        out_len += n;
        out[out_len] = 0;
    }
}

static void say_cr(void) {
    say("\n");
}

static void say_hex(const char *label, uint32_t v) {
    char b[48];
    snprintf(b, sizeof b, "%s%08lX", label, (unsigned long)v);
    say(b);
}

/* What the probes since `before` did: "no fault", or the last trap */
static void say_trap(uint32_t before) {
    if (pmp_traps == before) {
        say("no fault");
    } else {
        say_hex("fault (mcause ", pmp_cause);
        say_hex(", mtval ", pmp_tval);
        say(")");
    }
}

static void say_pmp(const char *indent) {
    say(indent);
    say_hex("pmpcfg0 ", CSR_READ(pmpcfg0));
    say_hex("  pmpaddr0 ", CSR_READ(pmpaddr0));
    say_hex(" 1 ", CSR_READ(pmpaddr1));
    say_hex(" 2 ", CSR_READ(pmpaddr2));
    say_hex(" 3 ", CSR_READ(pmpaddr3));
    say_cr();
}

/* Sets entry n's 8 bits of pmpcfg0, keeping the others */
static void set_cfg(int n, uint32_t cfg) {
    uint32_t c = CSR_READ(pmpcfg0);
    c = (c & ~(0xFFu << (8 * n))) | (cfg << (8 * n));
    CSR_WRITE(pmpcfg0, c);
    __asm volatile ("fence" ::: "memory");
}

/* Compares 64 words of two windows */
static void compare_windows(uintptr_t a, uintptr_t b) {
    uint32_t t = pmp_traps;
    int same = 0;
    for (int i = 0; i < 64; i++) {
        same += pmp_load(a + 4u * i) == pmp_load(b + 4u * i);
    }
    say_hex("   ", (uint32_t)a);
    say_hex(" and ", (uint32_t)b);
    say(": ");
    if (pmp_traps != t) {
        say_trap(t);
    } else {
        char n[40];
        snprintf(n, sizeof n, "%d of 64 words the same", same);
        say(n);
    }
    say_cr();
}

static void probe_buffer(const char *who, int user) {
    uintptr_t base = (uintptr_t)target;
    uint32_t t = pmp_traps;
    say("   ");
    say(who);
    say(" load: ");
    if (user) {
        (void)pmp_load_u(base);
    } else {
        (void)pmp_load(base);
    }
    say_trap(t);
    t = pmp_traps;
    say(", store: ");
    if (user) {
        pmp_store_u(base, 1);
    } else {
        pmp_store(base, 1);
    }
    say_trap(t);
    say_cr();
}

#define CFG_LOCK    0x80u
#define CFG_NAPOT   0x18u
#define CFG_RWX     0x07u

void pmp_test_m(void) {
    uint32_t saved_mepc = CSR_READ(mepc);
    uint32_t saved_mstatus = CSR_READ(mstatus);
    uint32_t saved_mtvec = CSR_READ(mtvec);
    mark("pmp: 0 in the trap handler\r\n");
    out_len = 0;
    out[0] = 0;

    say_hex("1. in machine mode by ecall; mstatus ", saved_mstatus);
    say_hex(" (MPP ", (saved_mstatus >> 11) & 3);
    say(": 0 = LiteForth runs in user mode)");
    say_cr();
    say_pmp("   ");
    uint32_t cfg = CSR_READ(pmpcfg0);
    if ((cfg >> 16) & CFG_LOCK) {
        say("   entry 2 is locked (an earlier pmp-test?): reset the board to run it again");
        say_cr();
        return;
    }
    if (((cfg >> 16) | (cfg >> 24)) & CFG_NAPOT) {
        say("   entry 2 or 3 is in use: not touching them");
        say_cr();
        return;
    }

    mark("pmp: 1 in machine mode\r\n");
    CSR_WRITE(mtvec, (uint32_t)(uintptr_t)pmp_trap);   /* mode 0: one entry for every trap */
    pmp_traps = 0;
    uint32_t t;

    uintptr_t base = (uintptr_t)target;
    CSR_WRITE(pmpaddr3, 0x1FFFFFFFu);                   /* NAPOT, all 4 GB */
    set_cfg(3, CFG_NAPOT | CFG_RWX);
    CSR_WRITE(pmpaddr2, (uint32_t)(base >> 2) | 3u);    /* NAPOT, 32 bytes */
    set_cfg(2, CFG_NAPOT);                              /* no R/W/X, not locked */
    mark("pmp: 2 entries set\r\n");
    say_hex("2. entry 2 over ", (uint32_t)base);
    say(" (32 bytes), no access, unlocked; entry 3 all memory, RWX:");
    say_cr();
    say_pmp("   ");
    target[0] = 0x12345678u;
    probe_buffer("machine-mode", 0);
    mark("pmp: 2 first fault due\r\n");
    probe_buffer("user-mode", 1);
    mark("pmp: 2 came back from it\r\n");
    say("   user-mode load elsewhere: ");
    t = pmp_traps;
    (void)pmp_load_u((uintptr_t)&elsewhere);
    say_trap(t);
    set_cfg(3, 0);                                      /* no entry matches it now */
    say("; with entry 3 off: ");
    t = pmp_traps;
    (void)pmp_load_u((uintptr_t)&elsewhere);
    say_trap(t);
    set_cfg(3, CFG_NAPOT | CFG_RWX);
    say_cr();

    mark("pmp: 3 locking\r\n");
    /* The last run locked up here, as if a locked entry turned PMP on for
       machine mode with everything else denied, the unlocked entry 3 not
       counting. So lock entry 3 (all memory, RWX) first and see whether
       machine mode survives that, then lock entry 2. */
    say("3. entry 3 (all memory, RWX) locked: ");
    set_cfg(3, CFG_LOCK | CFG_NAPOT | CFG_RWX);
    t = pmp_traps;
    (void)pmp_load((uintptr_t)&elsewhere);
    say("machine-mode load elsewhere: ");
    say_trap(t);
    say_cr();
    mark("pmp: 3 locking entry 2\r\n");
    say("   entry 2 locked too:");
    say_cr();
    set_cfg(2, CFG_LOCK | CFG_NAPOT);
    say_pmp("   ");
    probe_buffer("machine-mode", 0);
    say("   cleared (as far as it lets us): ");
    set_cfg(2, 0);
    CSR_WRITE(pmpaddr2, 0);
    say_pmp("");
    probe_buffer("machine-mode", 0);

    mark("pmp: 4 flash windows\r\n");
    say("4. the code flash's windows:");
    say_cr();
    compare_windows(0x00000000u, 0x08000000u);
    compare_windows(0x00010000u, 0x08010000u);

    mark("pmp: 5 V3F RAM\r\n");
    say("5. the V3F's RAM, 0x20100000: ");
    t = pmp_traps;
    say_hex("", pmp_load(0x20100000u));
    say(", ");
    say_trap(t);
    say_cr();

    mark("pmp: 6 V3F flash\r\n");
    say("6. running code in the V3F's flash: ");
    uintptr_t ret = 0;
    t = pmp_traps;
    for (uintptr_t a = 0x100; a < 0x4000 && !ret && pmp_traps == t; a += 4) {
        uint32_t w = pmp_load(a);                       /* past the vector table */
        if ((w & 0xFFFF) == 0x8082) {                   /* c.ret */
            ret = a;
        } else if ((w >> 16) == 0x8082) {
            ret = a + 2;
        }
    }
    if (pmp_traps != t) {
        say("reading it: ");
        say_trap(t);
    } else if (!ret) {
        say("no c.ret found in 0x100-0x3FFF");
    } else {
        say_hex("a c.ret at ", (uint32_t)ret);
        say(": ");
        t = pmp_traps;
        mark("pmp: 6 calling it\r\n");
        pmp_exec(ret);
        say_trap(t);
    }
    say_cr();

    mark("pmp: 7 done, back to user mode\r\n");
    CSR_WRITE(mtvec, saved_mtvec);
    CSR_WRITE(mstatus, saved_mstatus);
    CSR_WRITE(mepc, saved_mepc);
    say("Done. Entries 2 and 3 stay locked (entry 3 open: all memory) until a reset.");
    say_cr();
}

/* An ecall from user mode, vector 8 of the V5F's table. A plain RISC-V
   interrupt handler: it saves what it uses and ends with mret. */
void Ecall_U_Mode_Handler(void) __attribute__((interrupt));
void Ecall_U_Mode_Handler(void) {
    uint32_t mepc = CSR_READ(mepc);
    pmp_test_m();
    CSR_WRITE(mepc, mepc + 4);                          /* past the ecall */
}

int lfAPI_pmpTest(void) {
    out_len = 0;
    out[0] = 0;
    mark("pmp: ecall\r\n");
    /* Every caller-saved register clobbered, in case the handler's
       saving of them and the hardware stack (HPE) don't cover them all */
    __asm volatile ("ecall" ::: "memory", "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
                    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7");
    mark("pmp: back from the ecall\r\n");
    char *p = out;
    while (*p) {
        char *e = strchr(p, '\n');
        if (e) {
            *e = 0;
        }
        lf_puts(p);
        lfCR();
        if (!e) {
            break;
        }
        p = e + 1;
    }
    if (!out[0]) {
        lf_puts("pmp-test: the ecall came back without running the test");
        lfCR();
    }
    return 0;
}
