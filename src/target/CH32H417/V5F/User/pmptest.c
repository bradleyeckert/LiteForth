/*
 * pmp-test (pmptest.h): the V5F's PMP and what it can reach, with access
 * faults caught by a trap handler of our own instead of HardFault_Handler
 * (which would reset the chip).
 *
 * The handler and the probes are in assembly so their instructions are
 * known: each probe's load or store is one 4-byte instruction (no RVC), so
 * the handler skips a faulting one by adding 4 to mepc; a fault fetching
 * an instruction (exec probe) returns to the caller through ra instead.
 * Interrupts are off while mtvec points at the handler, and mtvec is put
 * back before they're on again. Output is kept in a buffer and printed
 * at the end, so nothing waits on the terminal while interrupts are off.
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
void pmp_exec(uintptr_t a);             /* calls a */
void pmp_trap(void);

__asm__(
"   .section .text.pmp_probes,\"ax\",@progbits\n"
"   .option push\n"
"   .option norvc\n"
"   .global pmp_load\n"
"   .align 2\n"
"pmp_load:\n"
"   mv   a1, a0\n"
"   lw   a0, 0(a1)\n"               /* faults: a0 stays the address */
"   ret\n"
"   .global pmp_store\n"
"pmp_store:\n"
"   sw   a1, 0(a0)\n"
"   ret\n"
"   .global pmp_exec\n"
"pmp_exec:\n"
"   jr   a0\n"                      /* returns to our caller through ra */
"   .global pmp_trap\n"
"   .align 6\n"                     /* mtvec: the base needs alignment */
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
"2: lw   t0, 0(sp)\n"
"   lw   t1, 4(sp)\n"
"   addi sp, sp, 16\n"
"   mret\n"
"   .option pop\n"
"   .text\n");

static uint32_t target[8] __attribute__((aligned(32)));    /* the locked entry's region */

#define CSR_READ(name) ({ uint32_t v_; __asm volatile ("csrr %0, " #name : "=r"(v_)); v_; })
#define CSR_WRITE(name, v) __asm volatile ("csrw " #name ", %0" :: "r"(v) : "memory")

static uint32_t old_mtvec, old_mstatus;

static void catch_on(void) {
    old_mstatus = CSR_READ(mstatus);
    __asm volatile ("csrci mstatus, 8" ::: "memory");     /* MIE off */
    old_mtvec = CSR_READ(mtvec);
    CSR_WRITE(mtvec, (uint32_t)(uintptr_t)pmp_trap);      /* mode 0: one entry for every trap */
    pmp_traps = 0;
}

static void catch_off(void) {
    CSR_WRITE(mtvec, old_mtvec);
    if (old_mstatus & 8) {
        __asm volatile ("csrsi mstatus, 8" ::: "memory");
    }
}

static char out[2048];
static size_t out_len;

static void say(const char *s) {
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

/* Prints what was said, one line at a time */
static void flush(void) {
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
    out_len = 0;
    out[0] = 0;
}

static void say_hex(const char *label, uint32_t v) {
    char b[48];
    snprintf(b, sizeof b, "%s%08lX", label, (unsigned long)v);
    say(b);
}

/* What the last probe did: "ok", or the trap's mcause and mtval */
static void say_trap(uint32_t before) {
    if (pmp_traps == before) {
        say("no fault");
    } else {
        say_hex("fault: mcause ", pmp_cause);
        say_hex(" mtval ", pmp_tval);
    }
}

static void say_pmp(void) {
    say_hex("pmpcfg0 ", CSR_READ(pmpcfg0));
    say_hex("  pmpaddr0 ", CSR_READ(pmpaddr0));
    say_hex(" 1 ", CSR_READ(pmpaddr1));
    say_hex(" 2 ", CSR_READ(pmpaddr2));
    say_hex(" 3 ", CSR_READ(pmpaddr3));
    say_cr();
}

/* Compares 64 words of two windows, through probes */
static void compare_windows(uintptr_t a, uintptr_t b) {
    uint32_t t0 = pmp_traps;
    int same = 0;
    for (int i = 0; i < 64; i++) {
        same += pmp_load(a + 4u * i) == pmp_load(b + 4u * i);
    }
    say_hex("  ", (uint32_t)a);
    say_hex(" and ", (uint32_t)b);
    say(": ");
    if (pmp_traps != t0) {
        say_trap(t0);
    } else {
        char n[40];
        snprintf(n, sizeof n, "%d of 64 words the same", same);
        say(n);
    }
    say_cr();
}

/* Entry 3's 8 bits of pmpcfg0 set to cfg, the others kept */
static void set_cfg3(uint32_t cfg) {
    uint32_t c = CSR_READ(pmpcfg0);
    c = (c & 0x00FFFFFFu) | (cfg << 24);
    CSR_WRITE(pmpcfg0, c);
    __asm volatile ("fence.i" ::: "memory");
}

int lfAPI_pmpTest(void) {
    say("1. ");
    say_pmp();
    uint32_t cfg3 = CSR_READ(pmpcfg0) >> 24;
    if (cfg3 & 0x80) {
        say("   entry 3 is locked (an earlier pmp-test?): reset the board to run it again");
        flush();
        return 0;
    }
    if (cfg3 & 0x18) {
        say("   entry 3 is in use: not touching it");
        flush();
        return 0;
    }

    catch_on();
    uint32_t t;

    say("2. the code flash's windows:");
    say_cr();
    compare_windows(0x00000000u, 0x08000000u);
    compare_windows(0x00010000u, 0x08010000u);

    say("3. the V3F's RAM, 0x20100000: ");
    t = pmp_traps;
    say_hex("", pmp_load(0x20100000u));
    say(", ");
    say_trap(t);
    say_cr();

    say("4. running code in the V3F's flash: ");
    uintptr_t ret = 0;
    t = pmp_traps;
    for (uintptr_t a = 0x100; a < 0x4000 && !ret && pmp_traps == t; a += 4) {
        uint32_t w = pmp_load(a);                        /* past the vector table */
        if ((w & 0xFFFF) == 0x8082) {                    /* c.ret */
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
        pmp_exec(ret);
        say_trap(t);
    }
    say_cr();

    uintptr_t base = (uintptr_t)target;
    uint32_t napot = (uint32_t)(base >> 2) | 3u;        /* 32 bytes */
    say_hex("5. entry 3 over ", (uint32_t)base);
    say(" (32 bytes), no access, unlocked:");
    say_cr();
    CSR_WRITE(pmpaddr3, napot);
    set_cfg3(0x18);                                     /* NAPOT, no R/W/X, not locked */
    say("   ");
    say_pmp();
    t = pmp_traps;
    target[0] = 0x12345678u;
    (void)pmp_load(base);
    say("   load: ");
    say_trap(t);
    t = pmp_traps;
    pmp_store(base, 1);
    say(", store: ");
    say_trap(t);
    say_cr();
    set_cfg3(0);
    CSR_WRITE(pmpaddr3, 0);

    say("6. the same, locked:");
    say_cr();
    CSR_WRITE(pmpaddr3, napot);
    set_cfg3(0x98);                                     /* L, NAPOT, no R/W/X */
    say("   ");
    say_pmp();
    t = pmp_traps;
    (void)pmp_load(base);
    say("   load: ");
    say_trap(t);
    t = pmp_traps;
    pmp_store(base, 2);
    say(", store: ");
    say_trap(t);
    say_cr();
    say("   cleared (as far as it lets us): ");
    set_cfg3(0);
    CSR_WRITE(pmpaddr3, 0);
    say_pmp();
    t = pmp_traps;
    (void)pmp_load(base);
    say("   load again: ");
    say_trap(t);
    say_cr();

    catch_off();
    say("Done. Entry 3 stays as it is until a reset.");
    flush();
    return 0;
}
