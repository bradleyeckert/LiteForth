/* Tests src/iomap.c through the VM: each `>sys` and `sys>` runs as one
   instruction (vmRun(1, inst, 0)) on values pushed onto the data stack,
   in the order a Forth word would run them. The serial port is
   a fake. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "iomap.h"

/* What iomap.c and vm.c call, faked */
uint32_t g_lf_sys_options;
int VMapi0Call(int fn) { (void)fn; return 0; }
int VMapi1Call(int fn) { (void)fn; return 0; }
uint64_t lfGetTimeMicroSec(void) { return 0; }
void lfWatchdogPing(void) { }
int vmPush(int32_t x) { return vmPoke(-1, x); }
int32_t vmPop(void) { return vmPeek(-1); }

static char sent[64];
static int sent_n, put_ior;
static const char *typed = "";
static int busy;
int lf_putc(char c) { if (sent_n < 63) sent[sent_n++] = c; return put_ior; }
int serial_getc(void) { return *typed ? (unsigned char)*typed++ : -1; }
int serial_ready(void) { return *typed != 0; }
int serial_busy(void) { return busy; }

uint64_t divide64by32(uint64_t dividend, uint32_t divisor, uint32_t *remainder) {
    *remainder = (uint32_t)(dividend % divisor);
    return dividend / divisor;
}

static int failures, checks;

static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static uint32_t stack_depth(void) { return lf_vm.sp; }

static int wr(int port) { return vmRun(1, VMI_TOSYS | port, 0); }
static int rd(int port) { return vmRun(1, VMI_FROMSYS | port, 0); }
static void push(uint32_t x) { vmPush((int32_t)x); }
static uint32_t pop(void) { return (uint32_t)vmPop(); }

/* `begin 37 sys> 0= until`: the number of busy reads */
static int wait(void) {
    int polls = 0;
    for (;;) {
        rd(IO_MATH_STATUS);
        if (pop() == 0) {
            return polls;
        }
        polls++;
    }
}

/* um* or m* ( a b -- lo hi ): 40 >sys 32/33 >sys wait 40 sys> 41 sys> */
static void mul(int port, uint32_t a, uint32_t b, uint32_t *lo, uint32_t *hi) {
    push(a);  push(b);
    wr(IO_MATH_ARG);  wr(port);
    wait();
    rd(IO_MATH_RES);  rd(IO_MATH_RES + 1);
    *hi = pop();  *lo = pop();
}

static void test_terminal(void) {
    sent_n = 0;
    push('O');  check(wr(IO_TERM_DATA) == 0, "emit");
    push('k');  wr(IO_TERM_DATA);
    check(sent_n == 2 && sent[0] == 'O' && sent[1] == 'k', "emit sends the characters");
    typed = "x";
    rd(IO_TERM_RXQ);  check(pop() != 0, "key? with a character waiting");
    rd(IO_TERM_DATA);  check(pop() == 'x', "key gives it");
    rd(IO_TERM_RXQ);  check(pop() == 0, "key? with none");
    busy = 1;  rd(IO_TERM_TXQ);  check(pop() == 1, "emit? while busy");
    busy = 0;  rd(IO_TERM_TXQ);  check(pop() == 0, "emit? when idle");
    put_ior = ERR_EXCEPTION_IN_CHAR;
    uint32_t depth = stack_depth();
    push('z');
    check(wr(IO_TERM_DATA) == ERR_EXCEPTION_IN_CHAR, "emit passes the port's error on");
    check(stack_depth() == depth, "a failed emit still takes its character");
    put_ior = 0;
}

static void test_multiply(void) {
    static const uint32_t v[] = { 0, 1, 2, 3, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 12345678, 0xDEADBEEF };
    int n = (int)(sizeof v / sizeof v[0]);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            uint32_t lo, hi;
            uint64_t want = (uint64_t)v[i] * v[j];
            mul(IO_MATH_UMSTAR, v[i], v[j], &lo, &hi);
            check(lo == (uint32_t)want && hi == (uint32_t)(want >> 32), "um*");
            int64_t swant = (int64_t)(int32_t)v[i] * (int32_t)v[j];
            mul(IO_MATH_MSTAR, v[i], v[j], &lo, &hi);
            check(lo == (uint32_t)swant && hi == (uint32_t)((uint64_t)swant >> 32), "m*");
        }
    }
}

static void test_mudivmod(void) {
    struct { uint64_t d; uint32_t u; } c[] = {
        { 100, 7 }, { 0xFFFFFFFFFFFFFFFFull, 1 }, { 0x123456789ABCDEF0ull, 0x10000 },
        { 0x00000005FFFFFFFFull, 3 }, { 0, 5 }, { 0xFFFFFFFFull, 0xFFFFFFFF },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        /* ( udl udh u -- r uql uqh ): 41 >sys 40 >sys 34 >sys wait 42 sys> 40 sys> 41 sys> */
        push((uint32_t)c[i].d);  push((uint32_t)(c[i].d >> 32));  push(c[i].u);
        wr(IO_MATH_ARG + 1);  wr(IO_MATH_ARG);  wr(IO_MATH_MUDIVMOD);
        wait();
        rd(IO_MATH_RES + 2);  rd(IO_MATH_RES);  rd(IO_MATH_RES + 1);
        uint32_t qh = pop(), ql = pop(), r = pop();
        uint64_t q = c[i].d / c[i].u;
        check(r == (uint32_t)(c[i].d % c[i].u) && ql == (uint32_t)q && qh == (uint32_t)(q >> 32),
              "mu/mod");
    }
}

static void test_stardivmod(void) {
    struct { int32_t n1, n2, n3, r, q; } c[] = {
        { 7, 2, 3, 2, 4 }, { -7, 2, 3, -2, -4 }, { 7, -2, 3, -2, -4 }, { 7, 2, -3, 2, -4 },
        { -7, -2, -3, 2, -4 }, { 1000000, 1000, 7, 6, 142857142 },
        { -2147483647 - 1, 1, 1, 0, -2147483647 - 1 }, { 0, 5, 9, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        /* ( n1 n2 n3 -- r q ): 41 >sys 40 >sys 35 >sys wait 42 sys> 40 sys> */
        push((uint32_t)c[i].n1);  push((uint32_t)c[i].n2);  push((uint32_t)c[i].n3);
        wr(IO_MATH_ARG + 1);  wr(IO_MATH_ARG);  wr(IO_MATH_STARDIVMOD);
        wait();
        rd(IO_MATH_RES + 2);  rd(IO_MATH_RES);
        int32_t q = (int32_t)pop(), r = (int32_t)pop();
        check(q == c[i].q && r == c[i].r, "star-slash-mod (symmetric)");
    }
}

static void test_errors(void) {
    uint32_t lo, hi;
    mul(IO_MATH_UMSTAR, 6, 7, &lo, &hi);  /* Y0 = 42, Y1 = 0 */
    for (int port = IO_MATH_MUDIVMOD; port <= IO_MATH_STARDIVMOD; port++) {
        uint32_t depth = stack_depth();
        push(10);  push(5);  push(0);   /* divisor 0 */
        wr(IO_MATH_ARG + 1);  wr(IO_MATH_ARG);
        check(wr(port) == ERR_DIVISION_BY_ZERO, "starting a division by zero");
        check(stack_depth() == depth, "takes its arguments");
        check(wait() == 0, "and leaves the unit idle");
        rd(IO_MATH_RES);
        check(pop() == 42, "with the results as they were");
    }

    uint32_t depth = stack_depth();
    check(rd(IOMAP_FIRST + 100) == ERR_INVALID_ADDRESS, "a port that can't be read");
    check(stack_depth() == depth + 1 && pop() == 0, "pushes 0");
    check(rd(IO_MATH_UMSTAR) == ERR_INVALID_ADDRESS, "an operation's port is write-only");
    pop();
    push(5);
    check(wr(IOMAP_FIRST + 100) == ERR_INVALID_ADDRESS, "a port that can't be written");
    push(6);
    check(wr(IO_TERM_RXQ) == ERR_INVALID_ADDRESS, "key? is read-only");
    push(7);
    check(wr(IO_MATH_STATUS) == ERR_INVALID_ADDRESS, "the status is read-only");
    push(8);
    check(wr(IO_MATH_ARG + 2) == ERR_INVALID_ADDRESS, "there are two argument registers");
    check(stack_depth() == depth, "a failed write still takes its value");
}

/* A slower unit: busy for a few status reads, and results refused meanwhile */
static void test_busy(void) {
    iomap_math_latency = 3;
    push(6);  push(7);
    wr(IO_MATH_ARG);  wr(IO_MATH_UMSTAR);
    uint32_t depth = stack_depth();
    for (int n = 0; n < 3; n++) {
        check(rd(IO_MATH_RES + n) == ERR_UNSUPPORTED_OPERATION, "a result while busy");
        check(stack_depth() == depth + 1 && pop() == 0, "pushes 0");
    }
    check(wait() == 3, "the status says busy, then ready");
    rd(IO_MATH_RES);  rd(IO_MATH_RES + 1);
    uint32_t hi = pop(), lo = pop();
    check(lo == 42 && hi == 0, "the results once it's ready");
    iomap_math_latency = 0;
    push(1);  push(2);
    wr(IO_MATH_ARG);  wr(IO_MATH_MSTAR);
    check(wait() == 0, "a unit that finishes at once is never busy");
    rd(IO_MATH_RES);  rd(IO_MATH_RES + 1);
    pop();  pop();
}

static void test_vm_indexes_unchanged(void) {
    lf_vm.X = 0x1234;
    rd(VMSFROM_X);
    check(pop() == 0x1234, "index 1 below IOMAP_FIRST is still x@");
    push(9);
    wr(VMSTO_SHIFT);
    check(lf_vm.shift_size == 9, "index 0 is still shft[");
}

int main(void) {
    lf_vm.sp = 0;
    test_terminal();
    test_multiply();
    test_mudivmod();
    test_stardivmod();
    test_errors();
    test_busy();
    test_vm_indexes_unchanged();
    if (failures) {
        printf("iomap: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("iomap tests passed (%d checks)\n", checks);
    return 0;
}
