/*
 * difftest: runs the VM on random states and code and prints a hash of the
 * resulting state for each case. Built natively with vm.c's C vmExec, and
 * for RV32 (run in rv32emu) with vm_rv32.S; the outputs must be the same.
 *
 * Cases (the same sequence in both builds, from a fixed seed):
 *   once  vmRun(1, inst, 0) on one random instruction
 *   step  vmRun(0, steps, 0) on random code (no unext, which could loop
 *         2^32 times inside one instruction)
 *   word  vmRun(0, 0, addr) on random straight-line code ending in a return
 * The API functions are stubs that push, pop, fail, remap memory or run a
 * nested word, so API calls and re-entry are covered too.
 *
 * Build with -DBENCH to time instead: it runs a few loops and prints the
 * RV32 instructions per VM instruction (`make bench`).
 * With -DDUMP=n it prints the whole state after case n.
 */
#include <stdint.h>
#include <stddef.h>
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "api0.h"
#include "lftime.h"

void out(const char* s, int n);         // rt_rv32.c, or below

#ifndef __riscv
#include <stdio.h>
void out(const char* s, int n) { fwrite(s, 1, (size_t)n, stdout); fflush(stdout); }
#endif

static void puts_(const char* s) { int n = 0; while (s[n]) n++; out(s, n); }
static void hex(uint32_t v) {
    char b[8];
    for (int i = 7; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    out(b, 8);
}
static void dec(uint32_t v) {
    char b[10]; int i = 10;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    out(b + i, 10 - i);
}

uint32_t g_lf_sys_options;

#define UNUSED_IN_BENCH __attribute__((unused))   // the case generators

/* ---- memory ------------------------------------------------------------ */

#define CODE_CELLS  2048                // page 0: code and data
#define EXEC_CELLS  1024                // executable part of page 0
#define WP0         512                 // page 0 is write-protected below this
#define RAM_CELLS   1024                // page 1: RAM
#define ROM_CELLS   16                  // page 7: read-only
#define NESTED      1016                // a word the API stub runs (cells)

static uint32_t page0[CODE_CELLS];
static uint32_t page1[RAM_CELLS];
static uint32_t page7[ROM_CELLS];

static uint32_t seed = 0x2545F491u;
static uint32_t rnd(void) {             // xorshift32
    seed ^= seed << 13;  seed ^= seed >> 17;  seed ^= seed << 5;
    return seed;
}
static uint32_t below(uint32_t n) { return rnd() % n; }

static void map_memory(void) {
    for (int i = 0; i < VM_MEM_PAGES; i++) {
        vm_memory[i] = NULL;
        vm_memory_rd_limit[i] = vm_memory_wp_limit[i] = vm_memory_executable[i] = 0;
    }
    vm_memory[0] = page0;
    vm_memory_rd_limit[0] = CODE_CELLS;
    vm_memory_wp_limit[0] = WP0;
    vm_memory_executable[0] = EXEC_CELLS;
    vm_memory[1] = page1;
    vm_memory_rd_limit[1] = RAM_CELLS;
    vm_memory_executable[1] = 64;
    vm_memory[7] = page7;
    vm_memory_rd_limit[7] = ROM_CELLS;
    vm_memory_wp_limit[7] = ROM_CELLS;
}

#define PAGE_SHIFT (22 - VM_LOG2_PAGES)

static uint32_t rnd_addr(void) {        // a cell or slice address, mostly valid
    static const uint8_t pages[8] = { 0, 0, 0, 1, 1, 1, 2, 7 };
    uint32_t page = pages[below(8)];
    uint32_t lim = (page == 0) ? CODE_CELLS : (page == 1) ? RAM_CELLS : ROM_CELLS;
    uint32_t a = (page << PAGE_SHIFT) | below(lim + 8);
    if (below(16) == 0) a = rnd() & 0x3FFFFF;
    if (below(4) == 0) {                // a slice: size 1..31, shift 0..31
        a |= ((1 + below(31)) << 27) | (below(32) << 22);
    }
    if (below(32) == 0) a = rnd();
    return a;
}

static uint32_t rnd_value(void) {
    switch (below(8)) {
    case 0: return 0;
    case 1: return below(16);
    case 2: return 0xFFFFFFFFu - below(4);
    case 3: return 0x80000000u ^ below(4);
    case 4: return rnd_addr();
    default: return rnd();
    }
}

/* ---- random instructions ----------------------------------------------- */

#define PC_CODE (EXEC_CELLS * 2)        // instruction addresses in page 0

static uint32_t rnd_uop(int no_rstack) {
    for (;;) {
        uint32_t u = below(32);
        if (u == VMU_UNEXT) continue;   // could loop 2^32 times
        if (no_rstack && (u == VMU_PUSH || u == VMU_POP)) continue;
        return u;
    }
}

/* safe = straight-line code for word calls: no control flow or return
   stack changes, so the word ends at its return. */
UNUSED_IN_BENCH static uint32_t rnd_inst(int safe) {
    uint32_t k = below(100);
    if (k < 45) {                       // micro-ops
        uint32_t s0 = rnd_uop(safe), s1 = rnd_uop(safe), s2;
        do { s2 = rnd_uop(safe); } while (s2 > 15);
        uint32_t inst = VM_UOPS | (s0 << 9) | (s1 << 4) | s2;
        if (!safe && below(6) == 0) inst |= VM_RET;
        return inst;
    }
    if (k < 60) return VMI_LIT | below(1 << 13);
    if (k < 70) {
        if (safe) return VMI_PFX | below(512);
        uint32_t target = below(10) ? below(PC_CODE) : below(1 << 13);
        return (below(2) ? VMI_CALL : VMI_JUMP) | (target & 0x1FFF);
    }
    for (;;) {                          // 9-bit immediate ops
        uint32_t op = below(16), imm = below(512);
        switch (op) {
        case VMO_ZBRAN: case VMO_BRAN: case VMO_PBRAN: case VMO_RCALL: case VMO_NEXT:
            if (safe) continue;
            if (below(4)) imm = (below(64) - 32) & 0x1FF;   // nearby
            break;
        case VMO_SYS:
            imm = below(8);             // 6 and 7 do nothing
            if (safe && imm == VMS_TASK) continue;
            if (imm == VMS_BREAK && below(3)) continue;
            break;
        case VMO_TOSYS:
            imm = below(3);
            if (imm == VMSTO_YEET && below(2)) continue;
            break;
        case VMO_FROMSYS:
            imm = below(4);
            if (safe && imm == VMSFROM_TASK) continue;
            break;
        case 5: case 7: case 11:        // invalid opcodes
            if (below(8)) continue;
            break;
        case VMO_API0: case VMO_API1:
            imm = below(16);
            if (below(2)) continue;
            break;
        default: break;
        }
        return VMI_OTHER | (op << 9) | imm;
    }
}

static void put_inst(uint32_t pc, uint32_t inst) {
    uint32_t* cell = &page0[pc >> 1];
    if (pc & 1) *cell = (*cell & 0xFFFFu) | (inst << 16);
    else        *cell = (*cell & 0xFFFF0000u) | (inst & 0xFFFFu);
}

/* ---- stubs ------------------------------------------------------------- */

static uint32_t api_sum, usec_calls, pings;

static int api_stub(int fn, uint32_t salt) {
    switch (fn & 7) {
    case 0: vmPoke(-1, (int32_t)(1000 + fn + salt)); return 0;
    case 1: api_sum += (uint32_t)vmPeek(-1) * 3 + salt; return 0;
    case 2: return -200 - fn;
    case 3:                             // remap: shrink or grow the code
        vm_memory_executable[0] = (vm_memory_executable[0] == EXEC_CELLS) ? 300 : EXEC_CELLS;
        return 0;
    case 4: return vmRun(0, 0, NESTED * 2);   // nested word: lit 7 + ;
    case 5: vmPoke(0, vmPeek(0) ^ (int32_t)salt); return 0;
    default: return 0;
    }
}
int VMapi0Call(int fn) { return api_stub(fn, 0x11); }
int VMapi1Call(int fn) { return api_stub(fn, 0x2200); }
uint64_t lfGetTimeMicroSec(void) {
    usec_calls++;
    return 0x123456789ull + 1000ull * usec_calls;
}
void lfWatchdogPing(void) { pings++; }

/* ---- state ------------------------------------------------------------- */

UNUSED_IN_BENCH static void rnd_state(void) {
    lf_vm.T = rnd_value();  lf_vm.N = rnd_value();  lf_vm.R = rnd_value();
    lf_vm.A = rnd_addr();   lf_vm.B = rnd_addr();   lf_vm.U = rnd_addr() & 0x3FFFFF;
    lf_vm.X = rnd();  lf_vm.Y = rnd();
    lf_vm.prefix = below(4) ? 0 : rnd_value();
    lf_vm.shift_size = below(32);
    lf_vm.sp = below(STACK_CAPACITY);
    lf_vm.rp = below(STACK_CAPACITY);
    lf_vm.cy = below(2);
    for (int i = 0; i < STACK_CAPACITY; i++) {
        lf_vm.datastack[i] = rnd_value();
        lf_vm.returnstack[i] = below(2) ? below(PC_CODE) : rnd_value();
    }
    for (int i = 0; i < CODE_CELLS; i++) page0[i] = rnd_value();
    for (int i = 0; i < RAM_CELLS; i++) page1[i] = rnd_value();
    for (int i = 0; i < ROM_CELLS; i++) page7[i] = rnd();
    page0[NESTED] = (VMI_LIT | 7) | ((uint32_t)(VM_UOPS | VM_RET | (VMU_PLUS << 9)) << 16);
    map_memory();
    g_lf_sys_options = 0;
    if (below(8) == 0) g_lf_sys_options = SYS_OPTION_ONLY_TERM;
    if (below(16) == 0) g_lf_sys_options = SYS_OPTION_NO_API;
}

static uint32_t hash;
static void mix(uint32_t v) {           // FNV-1a over the bytes
    for (int i = 0; i < 4; i++) { hash ^= (v >> (8 * i)) & 0xFF;  hash *= 16777619u; }
}

static uint32_t state_hash(void) {
    hash = 2166136261u;
    const uint32_t* p = (const uint32_t*)&lf_vm;
    for (size_t i = 0; i < sizeof lf_vm / 4; i++) mix(p[i]);
    for (int i = 0; i < CODE_CELLS; i++) mix(page0[i]);
    for (int i = 0; i < RAM_CELLS; i++) mix(page1[i]);
    mix(vm_memory_executable[0]);
    mix(api_sum);  mix(usec_calls);  mix(pings);
    return hash;
}

#ifdef DUMP
static void dump(void) {
    static const char* names[] = { "T", "N", "PC", "R", "A", "B", "U", "X", "Y",
                                   "prefix", "shift", "sp", "rp", "cy" };
    const uint32_t* p = (const uint32_t*)&lf_vm;
    for (int i = 0; i < 14; i++) { puts_(names[i]); puts_("="); hex(p[i]); puts_(" "); }
    puts_("\n");
    for (int i = 0; i < STACK_CAPACITY; i++) { hex(lf_vm.datastack[i]); puts_((i & 7) == 7 ? "\n" : " "); }
    for (int i = 0; i < STACK_CAPACITY; i++) { hex(lf_vm.returnstack[i]); puts_((i & 7) == 7 ? "\n" : " "); }
    puts_("api "); hex(api_sum); puts_(" usec "); hex(usec_calls); puts_(" pings "); hex(pings);
    puts_(" exec0 "); hex(vm_memory_executable[0]); puts_("\n");
}
#endif

UNUSED_IN_BENCH static void report(const char* kind, uint32_t n, int32_t ior) {
    puts_(kind); puts_(" "); dec(n); puts_(" ior "); dec((uint32_t)ior);
    puts_(" hash "); hex(state_hash()); puts_("\n");
#ifdef DUMP
    if (n == DUMP) dump();
#endif
}

#ifndef BENCH

#ifndef CASES
#define CASES 20000
#endif

int main(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < CASES; i++, n++) {                 // once
        rnd_state();
        uint32_t inst = rnd_inst(0);
        if (below(8) == 0) {            // unext with a small count (and no
                                        // >r or r> before it to change R)
            uint32_t last;              // one unext only: after it, R is random
            do { last = rnd_uop(0); } while (last > 15);
            inst = VM_UOPS | (rnd_uop(1) << 9) | (VMU_UNEXT << 4) | last;
            if (below(2)) inst = VM_UOPS | (VMU_UNEXT << 9) | (rnd_uop(0) << 4);
            lf_vm.R = 1 + below(20);     // 0 would loop 2^32 times
        }
        lf_vm.PC = below(4) ? below(PC_CODE) : rnd();
        report("once", n, vmRun(1, inst, 0));
    }
    for (uint32_t i = 0; i < CASES; i++, n++) {                 // step
        rnd_state();
        for (uint32_t pc = 0; pc < PC_CODE; pc++) {
            if (pc != NESTED * 2 && pc != NESTED * 2 + 1) put_inst(pc, rnd_inst(0));
        }
        lf_vm.PC = below(8) ? below(PC_CODE) : rnd_value();
        uint32_t iors = 0;              // the app keeps running after errors
        int32_t ior = 0;                // (in its yeet handler), as from loadTIB
        for (int k = 0; k < 16; k++) {
            ior = vmRun(0, 1 + below(100), 0);
            iors = iors * 31 + (uint32_t)ior;
        }
        report("step", n, ior);
        puts_("  iors "); hex(iors); puts_("\n");
    }
    for (uint32_t i = 0; i < CASES; i++, n++) {                 // word
        rnd_state();
        uint32_t start = below(PC_CODE - 64), len = below(40), pc = start;
        for (uint32_t k = 0; k < len; k++) put_inst(pc++, rnd_inst(1));
        put_inst(pc, VM_UOPS | VM_RET);
        if (below(4)) lf_vm.A = below(RAM_CELLS) | (1 << PAGE_SHIFT);    // valid
        if (below(4)) lf_vm.B = WP0 + below(CODE_CELLS - WP0);
        report("word", n, vmRun(0, 0, (int32_t)start));
    }
    return 0;
}

#else /* BENCH */

#ifdef __riscv
static uint32_t instret(void) { uint32_t v; __asm__ volatile("csrr %0, instret" : "=r"(v)); return v; }
#else
static uint32_t instret(void) { return 0; }
#endif

/* Runs code at pc 0.. as a word and returns the RV32 instructions used. */
static uint32_t time_word(const uint16_t* code, int len, uint32_t* vm_steps) {
    map_memory();
    for (int i = 0; i < len; i++) put_inst((uint32_t)i, code[i]);
    lf_vm.sp = lf_vm.rp = 8;
    uint32_t t0 = instret();
    int32_t ior = vmRun(0, 0, 0);
    uint32_t t1 = instret();
    if (ior) { puts_("bench ior "); dec((uint32_t)ior); puts_("\n"); }
    (void)vm_steps;
    return t1 - t0;
}

#define U3(a, b, c) (VM_UOPS | ((a) << 9) | ((b) << 4) | (c))
#define LIT(n)      (VMI_LIT | (n))
#define NEXT(off)   (VMI_NEXT | ((off) & 0x1FF))
#define ZBRAN(off)  (VMI_ZBRAN | ((off) & 0x1FF))
#define CALL(a)     (VMI_CALL | (a))

static void bench(const char* name, const uint16_t* code, int len, uint32_t vm_insts) {
    uint32_t n = time_word(code, len, 0);
    puts_(name); puts_(": "); dec(n); puts_(" RV32 instructions, ");
    dec(vm_insts); puts_(" VM instructions, ");
    dec(n * 10 / vm_insts); puts_(" per 10 VM instructions\n");
}

int main(void) {
    // 1000 for ... next around: dup drop (one uop group)
    static const uint16_t loop1[] = {
        LIT(1000), U3(VMU_PUSH, 0, 0),
        U3(VMU_DUP, VMU_DROP, 0), NEXT(-2),
        VM_UOPS | VM_RET };
    bench("dup drop loop", loop1, 5, 2 + 2 * 1000 + 1);
    // 1000 for: a fetch, add, store (@a + !a) next
    static const uint16_t loop2[] = {
        LIT(1000), U3(VMU_PUSH, 0, 0), LIT(600), U3(VMU_ASTORE, 0, 0), LIT(0),
        U3(VMU_FETCHA, VMU_PLUS, 0), U3(VMU_DUP, VMU_STOREA, 0), NEXT(-3),
        VM_UOPS | VM_RET | (VMU_DROP << 9) };
    bench("@a + !a loop", loop2, 9, 5 + 3 * 1000 + 1);
    // 1000 for: call a word that is just ; next
    static const uint16_t loop3[] = {
        LIT(1000), U3(VMU_PUSH, 0, 0), CALL(6), NEXT(-2), VM_UOPS | VM_RET, 0,
        VM_UOPS | VM_RET };
    bench("call ; loop", loop3, 7, 2 + 3 * 1000 + 1);
    // 1000 for: lit lit + drop next
    static const uint16_t loop4[] = {
        LIT(1000), U3(VMU_PUSH, 0, 0), LIT(3), LIT(4), U3(VMU_PLUS, VMU_DROP, 0), NEXT(-4),
        VM_UOPS | VM_RET };
    bench("lit lit + drop loop", loop4, 7, 2 + 4 * 1000 + 1);
    return 0;
}

#endif /* BENCH */
