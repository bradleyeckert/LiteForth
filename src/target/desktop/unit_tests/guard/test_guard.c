/*
 * Tests for the V5F's machine-mode guard checks (V5F/User/guard.c): the
 * store decoder the HardFault handler uses to emulate user-mode writes to
 * RCC and CRC, which store it lets through, and the Guarded DMA buffer
 * rule.
 *
 * The standard encodings come from llvm-mc (--triple=riscv32 -mattr=+c);
 * WCH's XW ones from LLVM's own test llvm/test/MC/RISCV/xwchc-valid.s
 * (release/19.x), which this clang is too old to assemble.
 */
#include <stdio.h>
#include <string.h>
#include "guard.h"

static int failures, checks;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static uint32_t regs[32];

static void set_regs(void) {
    for (int i = 0; i < 32; i++) {
        regs[i] = 0x10203040u * (uint32_t)i + 0x01F00F80u;
    }
    regs[0] = 0;
}

typedef struct {
    uint32_t insn;
    int size, base;
    int32_t offset;
    int src, len;
    const char *text;
} store_case;

static const store_case stores[] = {
    /* llvm-mc */
    { 0xFEF2AE23, 4, 5, -4, 15, 4, "sw a5, -4(t0)" },
    { 0x7FFDAFA3, 4, 27, 2047, 31, 4, "sw t6, 2047(s11)" },
    { 0x80152023, 4, 10, -2048, 1, 4, "sw ra, -2048(a0)" },
    { 0x00F29323, 2, 5, 6, 15, 4, "sh a5, 6(t0)" },
    { 0xFE011F23, 2, 2, -2, 0, 4, "sh zero, -2(sp)" },
    { 0x00F280A3, 1, 5, 1, 15, 4, "sb a5, 1(t0)" },
    { 0xFFC18FA3, 1, 3, -1, 28, 4, "sb t3, -1(gp)" },
    { 0x0000C1C8, 4, 11, 4, 10, 2, "c.sw a0, 4(a1)" },
    { 0x0000DFE4, 4, 15, 124, 9, 2, "c.sw s1, 124(a5)" },
    { 0x0000C03C, 4, 8, 64, 15, 2, "c.sw a5, 64(s0)" },
    { 0x0000C43E, 4, 2, 8, 15, 2, "c.swsp a5, 8(sp)" },
    { 0x0000DFFE, 4, 2, 252, 31, 2, "c.swsp t6, 252(sp)" },
    { 0x0000C006, 4, 2, 0, 1, 2, "c.swsp ra, 0(sp)" },
    /* a compressed one with junk above bit 15: ignored */
    { 0xABCDC1C8, 4, 11, 4, 10, 2, "c.sw a0, 4(a1), high half set" },
    /* xwchc-valid.s */
    { 0xa000, 1, 8, 0, 8, 2, "qk.c.sb s0, 0(s0)" },
    { 0xb000, 1, 8, 1, 8, 2, "qk.c.sb s0, 1(s0)" },
    { 0xa020, 1, 8, 2, 8, 2, "qk.c.sb s0, 2(s0)" },
    { 0xa040, 1, 8, 4, 8, 2, "qk.c.sb s0, 4(s0)" },
    { 0xa400, 1, 8, 8, 8, 2, "qk.c.sb s0, 8(s0)" },
    { 0xa800, 1, 8, 16, 8, 2, "qk.c.sb s0, 16(s0)" },
    { 0xa002, 2, 8, 0, 8, 2, "qk.c.sh s0, 0(s0)" },
    { 0xa022, 2, 8, 2, 8, 2, "qk.c.sh s0, 2(s0)" },
    { 0xa042, 2, 8, 4, 8, 2, "qk.c.sh s0, 4(s0)" },
    { 0xa402, 2, 8, 8, 8, 2, "qk.c.sh s0, 8(s0)" },
    { 0xa802, 2, 8, 16, 8, 2, "qk.c.sh s0, 16(s0)" },
    { 0xb002, 2, 8, 32, 8, 2, "qk.c.sh s0, 32(s0)" },
    { 0x8040, 1, 2, 0, 8, 2, "qk.c.sbsp s0, 0(sp)" },
    { 0x80c0, 1, 2, 1, 8, 2, "qk.c.sbsp s0, 1(sp)" },
    { 0x8140, 1, 2, 2, 8, 2, "qk.c.sbsp s0, 2(sp)" },
    { 0x8240, 1, 2, 4, 8, 2, "qk.c.sbsp s0, 4(sp)" },
    { 0x8440, 1, 2, 8, 8, 2, "qk.c.sbsp s0, 8(sp)" },
    { 0x8060, 2, 2, 0, 8, 2, "qk.c.shsp s0, 0(sp)" },
    { 0x8160, 2, 2, 2, 8, 2, "qk.c.shsp s0, 2(sp)" },
    { 0x8260, 2, 2, 4, 8, 2, "qk.c.shsp s0, 4(sp)" },
    { 0x8460, 2, 2, 8, 8, 2, "qk.c.shsp s0, 8(sp)" },
    { 0x80e0, 2, 2, 16, 8, 2, "qk.c.shsp s0, 16(sp)" },
    /* XW with other registers: the CS fields are C.SW's (rs1' 9:7,
       rs2' 4:2), so qk.c.sb a0, 4(a1) is c.sw a0, 4(a1) = 0xC1C8 with
       funct3 101 and imm{2-1} in 6:5 (0b10): 0xA1C8. */
    { 0xa1c8, 1, 11, 4, 10, 2, "qk.c.sb a0, 4(a1) (by hand)" },
    { 0xa1ca, 2, 11, 4, 10, 2, "qk.c.sh a0, 4(a1) (by hand)" },
    { 0x805c, 1, 2, 0, 15, 2, "qk.c.sbsp a5, 0(sp) (by hand)" },
};

typedef struct {
    uint32_t insn;
    const char *text;
} other_case;

static const other_case others[] = {
    { 0x0005A503, "lw a0, 0(a1)" },
    { 0x00000013, "nop (addi)" },
    { 0x00F2B023, "sd: funct3 3, not RV32" },
    { 0x00F2C023, "funct3 4 store: not a store we know" },
    { 0x00F2A027, "fsw" },
    { 0x40F2A02F, "amoor.w" },
    { 0x4188, "c.lw a0, 0(a1)" },
    { 0x4502, "c.lwsp a0, 0(sp)" },
    { 0xE188, "c.fsw" },
    { 0xE02A, "c.fswsp" },
    { 0x2000, "qk.c.lbu s0, 0(s0)" },
    { 0x2002, "qk.c.lhu s0, 0(s0)" },
    { 0x8000, "qk.c.lbusp s0, 0(sp)" },
    { 0x8020, "qk.c.lhusp s0, 0(sp)" },
    { 0x9040, "Q0 funct3 100 with bit 12 set: reserved, not an XW store" },
    { 0x8840, "Q0 funct3 100 with bit 11 set: reserved" },
    { 0x0000, "the illegal all-zero halfword" },
};

static void test_decode(void) {
    set_regs();
    for (size_t i = 0; i < sizeof stores / sizeof stores[0]; i++) {
        const store_case *t = &stores[i];
        guard_store s;
        memset(&s, 0xA5, sizeof s);
        int r = guard_store_decode(t->insn, regs, &s);
        CHECK(r == 0, "%s: not decoded", t->text);
        if (r) continue;
        uint32_t v = regs[t->src];
        uint32_t want = t->size == 4 ? v : t->size == 2 ? (v & 0xFFFF) : (v & 0xFF);
        CHECK(s.addr == regs[t->base] + (uint32_t)t->offset, "%s: addr %08X, want %08X",
              t->text, s.addr, regs[t->base] + (uint32_t)t->offset);
        CHECK(s.value == want, "%s: value %08X, want %08X", t->text, s.value, want);
        CHECK(s.size == t->size, "%s: size %d, want %d", t->text, s.size, t->size);
        CHECK(s.len == t->len, "%s: len %d, want %d", t->text, s.len, t->len);
    }
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        guard_store s;
        CHECK(guard_store_decode(others[i].insn, regs, &s) == -1, "%s decoded as a store",
              others[i].text);
    }
}

static int allowed(uint32_t addr, int size) {
    guard_store s = { addr, 0, size, 4 };
    return guard_store_allowed(&s);
}

static void test_allowed(void) {
    /* RCC and CRC, whole */
    CHECK(allowed(0x40021000, 4), "RCC's first word");
    CHECK(allowed(0x40021018, 4), "RCC_HB2PCENR-ish");
    CHECK(allowed(0x400213FC, 4), "RCC window's last word");
    CHECK(allowed(0x400213FF, 1), "RCC window's last byte");
    CHECK(allowed(0x400213FE, 2), "RCC window's last half");
    CHECK(allowed(0x40023000, 4), "CRC_DATAR");
    CHECK(allowed(0x40023004, 1), "CRC_IDATAR");
    CHECK(allowed(0x40023008, 4), "CRC_CTLR");
    /* just outside */
    CHECK(!allowed(0x40021400, 4), "past RCC's window");
    CHECK(!allowed(0x40020FFC, 4), "just below RCC: DMAMUX's 4K slot");
    CHECK(!allowed(0x40023400, 4), "USBFS");
    CHECK(!allowed(0x40022FFC, 4), "flash controller's last word");
    /* what the entry is for */
    CHECK(!allowed(0x40020000, 4), "DMA1_INTFR");
    CHECK(!allowed(0x40020008, 4), "DMA1 channel 1 CFGR");
    CHECK(!allowed(0x40020408, 4), "DMA2 channel 1 CFGR");
    CHECK(!allowed(0x40020414, 4), "DMA2 channel 1 MADDR");
    CHECK(!allowed(0x40020800, 4), "DMAMUX");
    CHECK(!allowed(0x40020808, 1), "DMAMUX CFGR8_11, a byte");
    CHECK(!allowed(0x40022000, 4), "FLASH");
    CHECK(!allowed(0x40023C00, 4), "RNG");
    CHECK(!allowed(0x08070000, 4), "the board record");
    CHECK(!allowed(0x200C1000, 4), "DTCM");
    CHECK(!allowed(0, 4), "address 0");
    /* misaligned, or a size that isn't one */
    CHECK(!allowed(0x40021002, 4), "misaligned word");
    CHECK(!allowed(0x40021001, 2), "misaligned half");
    CHECK(!allowed(0x40021000, 3), "size 3");
    CHECK(!allowed(0x40021000, 8), "size 8");
    CHECK(!allowed(0x40021000, 0), "size 0");
    /* wrap-around */
    CHECK(!allowed(0xFFFFFFFC, 4), "top of memory");
}

static void test_buffer(void) {
    const uint32_t lo = 0x200C0300, hi = 0x200FC000;      /* .data/.bss, below the stack */
    const uint32_t xlo = 0x200D0000, xhi = 0x200D8000;    /* the DMA buffer, 32 KB */
    CHECK(guard_buffer_ok(lo, 4096, lo, hi, xlo, xhi), "at the start");
    CHECK(guard_buffer_ok(hi - 4096, 4096, lo, hi, xlo, xhi), "at the end");
    CHECK(guard_buffer_ok(xlo - 4096, 4096, lo, hi, xlo, xhi), "just below the DMA buffer");
    CHECK(guard_buffer_ok(xhi, 4096, lo, hi, xlo, xhi), "just above the DMA buffer");
    CHECK(guard_buffer_ok(lo, hi - lo, lo, hi, 0, 0), "all of it, nothing excluded");
    CHECK(!guard_buffer_ok(lo, 0, lo, hi, xlo, xhi), "empty");
    CHECK(!guard_buffer_ok(lo + 2, 512, lo, hi, xlo, xhi), "misaligned");
    CHECK(!guard_buffer_ok(lo - 4, 512, lo, hi, xlo, xhi), "starts below");
    CHECK(!guard_buffer_ok(hi - 508, 512, lo, hi, xlo, xhi), "runs past the end (the stack)");
    CHECK(!guard_buffer_ok(hi, 512, lo, hi, xlo, xhi), "in the stack");
    CHECK(!guard_buffer_ok(xlo, 512, lo, hi, xlo, xhi), "in the DMA buffer");
    CHECK(!guard_buffer_ok(xlo - 4, 8, lo, hi, xlo, xhi), "overlaps the DMA buffer's start");
    CHECK(!guard_buffer_ok(xhi - 4, 8, lo, hi, xlo, xhi), "overlaps the DMA buffer's end");
    CHECK(!guard_buffer_ok(xlo - 4, xhi - xlo + 8, lo, hi, xlo, xhi), "covers the DMA buffer");
    CHECK(!guard_buffer_ok(0x40020000, 512, lo, hi, xlo, xhi), "DMA registers");
    CHECK(!guard_buffer_ok(0x40000000, 4096, lo, hi, xlo, xhi), "the I/O page");
    CHECK(!guard_buffer_ok(0x08070000, 512, lo, hi, xlo, xhi), "the board record");
    CHECK(!guard_buffer_ok(0x200A0000, 512, lo, hi, xlo, xhi), "ITCM code");
    CHECK(!guard_buffer_ok(lo, 0xFFFFFFFCu, lo, hi, xlo, xhi), "huge n");
    CHECK(!guard_buffer_ok(0xFFFFFF00u, 0x200, 0, 0xFFFFFFFFu, 0, 0), "wraps around");
}

int main(void) {
    test_decode();
    test_allowed();
    test_buffer();
    printf("guard: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
