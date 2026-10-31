/*
 * The machine-mode guards' checks, in plain C: see guard.h.
 */
#include "guard.h"

#define REG_SP  2

static uint32_t bits(uint32_t x, int hi, int lo) {
    return (x >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

/* A 3-bit register field of a compressed instruction: x8..x15 */
static int creg(uint32_t c, int lo) {
    return 8 + (int)bits(c, lo + 2, lo);
}

static void fill(guard_store *s, const uint32_t regs[32], int rs1, uint32_t offset,
                 int rs2, int size, int len) {
    uint32_t v = regs[rs2];
    s->addr = regs[rs1] + offset;
    s->value = (size == 4) ? v : (size == 2) ? (v & 0xFFFFu) : (v & 0xFFu);
    s->size = size;
    s->len = len;
}

int guard_store_decode(uint32_t insn, const uint32_t regs[32], guard_store *s) {
    if ((insn & 3u) == 3u) {                        /* 32 bits */
        if (bits(insn, 6, 0) != 0x23u) {            /* STORE */
            return -1;
        }
        uint32_t f3 = bits(insn, 14, 12);
        if (f3 > 2) {
            return -1;                              /* SB SH SW only */
        }
        int32_t imm = ((int32_t)(insn & 0xFE000000u) >> 20) | (int32_t)bits(insn, 11, 7);
        fill(s, regs, (int)bits(insn, 19, 15), (uint32_t)imm, (int)bits(insn, 24, 20),
             1 << f3, 4);
        return 0;
    }
    uint32_t c = insn & 0xFFFFu;
    uint32_t op = c & 3u, f3 = bits(c, 15, 13);
    if (op == 0 && f3 == 6) {                       /* C.SW */
        uint32_t off = bits(c, 12, 10) << 3 | bits(c, 6, 6) << 2 | bits(c, 5, 5) << 6;
        fill(s, regs, creg(c, 7), off, creg(c, 2), 4, 2);
        return 0;
    }
    if (op == 2 && f3 == 6) {                       /* C.SWSP */
        uint32_t off = bits(c, 12, 9) << 2 | bits(c, 8, 7) << 6;
        fill(s, regs, REG_SP, off, (int)bits(c, 6, 2), 4, 2);
        return 0;
    }
    if (op == 0 && f3 == 5) {                       /* QK.C.SB (XW) */
        uint32_t off = bits(c, 12, 12) | bits(c, 11, 10) << 3 | bits(c, 6, 5) << 1;
        fill(s, regs, creg(c, 7), off, creg(c, 2), 1, 2);
        return 0;
    }
    if (op == 2 && f3 == 5) {                       /* QK.C.SH (XW) */
        uint32_t off = bits(c, 12, 10) << 3 | bits(c, 6, 5) << 1;
        fill(s, regs, creg(c, 7), off, creg(c, 2), 2, 2);
        return 0;
    }
    if (op == 0 && bits(c, 15, 11) == 0x10u) {      /* XW's stack forms */
        uint32_t f2 = bits(c, 6, 5);
        if (f2 == 2) {                              /* QK.C.SBSP */
            fill(s, regs, REG_SP, bits(c, 10, 7), creg(c, 2), 1, 2);
            return 0;
        }
        if (f2 == 3) {                              /* QK.C.SHSP */
            uint32_t off = bits(c, 10, 8) << 1 | bits(c, 7, 7) << 4;
            fill(s, regs, REG_SP, off, creg(c, 2), 2, 2);
            return 0;
        }
    }
    return -1;                                      /* loads, FP stores, ... */
}

static int within(uint32_t addr, int size, uint32_t base) {
    return addr >= base && addr - base <= GUARD_WINDOW_SIZE - (uint32_t)size;
}

int guard_store_allowed(const guard_store *s) {
    int size = s->size;
    if ((size != 1 && size != 2 && size != 4) || (s->addr & (uint32_t)(size - 1))) {
        return 0;
    }
    return within(s->addr, size, GUARD_RCC_BASE) || within(s->addr, size, GUARD_CRC_BASE);
}

int guard_rcc_turns_on_master(const guard_store *s, uint32_t current) {
    uint32_t reg = s->addr & ~3u;
    uint32_t mask;
    if (reg == GUARD_RCC_HBPCENR) {
        mask = GUARD_HB_MASTERS;
    } else if (reg == GUARD_RCC_HB2PCENR) {
        mask = GUARD_HB2_MASTERS;
    } else {
        return 0;                       /* not a clock-enable register */
    }
    /* merge the store into the register's current value (s->value is already
       truncated to s->size; the address is aligned, so the field doesn't
       cross the word) */
    uint32_t shift = (s->addr & 3u) * 8u;
    uint32_t post;
    if (s->size == 4) {
        post = s->value;
    } else {
        uint32_t field = ((s->size == 2) ? 0xFFFFu : 0xFFu) << shift;
        post = (current & ~field) | ((s->value << shift) & field);
    }
    return (post & mask) != 0;          /* a fenced master's clock would be on */
}

int guard_buffer_ok(uint32_t p, uint32_t n, uint32_t lo, uint32_t hi,
                    uint32_t xlo, uint32_t xhi) {
    if (n == 0 || (p & 3u) || p < lo || p >= hi || n > hi - p) {
        return 0;
    }
    return p >= xhi || p + n <= xlo;                /* no overflow: p + n <= hi */
}
