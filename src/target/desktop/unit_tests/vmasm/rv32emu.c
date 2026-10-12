/*
 * rv32emu: a small RV32IMC emulator for testing vm_rv32.S on the desktop.
 *
 * Usage: rv32emu program.elf
 *
 * Loads a statically linked RV32 ELF (PT_LOAD segments) into a flat memory
 * at address 0 and runs it from its entry point with sp at the top of the
 * memory. Supported: RV32I, M, C, `fence` (a no-op), `csrr` of cycle,
 * instret and time (all the retired instruction count), and `ecall` with
 * a7 = 64 (write a2 bytes from a1 to stdout; a0 is ignored) or 93 (exit
 * with code a0). Anything else, or a memory access outside the memory,
 * stops it with a message and exit code 99.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_SIZE (32u << 20)

static uint8_t* mem;
static uint32_t x[32];
static uint32_t pc;
static uint64_t retired;
static uint32_t ilen;                   // 4, or 2 for a compressed instruction

static void fail(const char* msg, uint32_t value) {
    fflush(stdout);
    fprintf(stderr, "rv32emu: %s %08X at pc %08X after %llu instructions\n",
            msg, value, pc, (unsigned long long)retired);
    exit(99);
}

static uint8_t* at(uint32_t addr, uint32_t size) {
    if ((addr >= MEM_SIZE) || (MEM_SIZE - addr < size)) {
        fail("bad address", addr);
    }
    if (addr & (size - 1)) {
        fail("misaligned access", addr);
    }
    return mem + addr;
}

static uint32_t load(uint32_t addr, int size, int is_signed) {
    uint8_t* p = at(addr, (uint32_t)size);
    uint32_t v = 0;
    for (int i = size - 1; i >= 0; i--) v = (v << 8) | p[i];
    if (is_signed && size < 4) {
        int shift = 32 - 8 * size;
        v = (uint32_t)((int32_t)(v << shift) >> shift);
    }
    return v;
}

static void store(uint32_t addr, int size, uint32_t v) {
    uint8_t* p = at(addr, (uint32_t)size);
    for (int i = 0; i < size; i++) { p[i] = (uint8_t)v; v >>= 8; }
}

static uint32_t bits(uint32_t v, int hi, int lo) {
    return (v >> lo) & ((1u << (hi - lo + 1)) - 1);
}

static int32_t sext(uint32_t v, int width) {
    return (int32_t)(v << (32 - width)) >> (32 - width);
}

static void ecall(void) {
    switch (x[17]) {
    case 64: {
        uint32_t len = x[12];
        fwrite(at(x[11], 1), 1, len, stdout);  // checks the start only
        if (x[11] + len > MEM_SIZE) fail("bad write buffer", x[11]);
        x[10] = len;
        break;
    }
    case 93:
        fflush(stdout);
        exit((int)x[10]);
    default:
        fail("unknown ecall", x[17]);
    }
}

static uint32_t mulh(int32_t a, int32_t b) {
    return (uint32_t)(((int64_t)a * (int64_t)b) >> 32);
}

/* Runs one instruction in its 32-bit form (ilen bytes long). Returns the
   next pc. */
static uint32_t exec32(uint32_t in) {
    uint32_t op = in & 0x7F, rd = bits(in, 11, 7), f3 = bits(in, 14, 12);
    uint32_t a = x[bits(in, 19, 15)], b = x[bits(in, 24, 20)];
    uint32_t f7 = bits(in, 31, 25);
    int32_t immi = sext(in >> 20, 12);
    uint32_t next = pc + ilen, v = 0;
    int write = 1;
    switch (op) {
    case 0x37: v = in & 0xFFFFF000u; break;                    // lui
    case 0x17: v = pc + (in & 0xFFFFF000u); break;             // auipc
    case 0x6F:                                                 // jal
        v = next;
        next = pc + (uint32_t)sext((bits(in, 31, 31) << 20) | (bits(in, 19, 12) << 12)
                     | (bits(in, 20, 20) << 11) | (bits(in, 30, 21) << 1), 21);
        break;
    case 0x67:                                                 // jalr
        v = next;
        next = (a + (uint32_t)immi) & ~1u;
        break;
    case 0x63: {                                               // branches
        int take;
        switch (f3) {
        case 0: take = (a == b); break;
        case 1: take = (a != b); break;
        case 4: take = ((int32_t)a < (int32_t)b); break;
        case 5: take = ((int32_t)a >= (int32_t)b); break;
        case 6: take = (a < b); break;
        case 7: take = (a >= b); break;
        default: fail("bad branch", in); take = 0;
        }
        if (take) {
            next = pc + (uint32_t)sext((bits(in, 31, 31) << 12) | (bits(in, 7, 7) << 11)
                         | (bits(in, 30, 25) << 5) | (bits(in, 11, 8) << 1), 13);
        }
        write = 0;
        break;
    }
    case 0x03: {                                               // loads
        uint32_t addr = a + (uint32_t)immi;
        switch (f3) {
        case 0: v = load(addr, 1, 1); break;
        case 1: v = load(addr, 2, 1); break;
        case 2: v = load(addr, 4, 0); break;
        case 4: v = load(addr, 1, 0); break;
        case 5: v = load(addr, 2, 0); break;
        default: fail("bad load", in);
        }
        break;
    }
    case 0x23: {                                               // stores
        uint32_t addr = a + (uint32_t)sext((f7 << 5) | rd, 12);
        if (f3 > 2) fail("bad store", in);
        store(addr, 1 << f3, b);
        write = 0;
        break;
    }
    case 0x13:                                                 // op-imm
        switch (f3) {
        case 0: v = a + (uint32_t)immi; break;
        case 1: v = a << (immi & 31); break;
        case 2: v = ((int32_t)a < immi); break;
        case 3: v = (a < (uint32_t)immi); break;
        case 4: v = a ^ (uint32_t)immi; break;
        case 5: v = (f7 & 0x20) ? (uint32_t)((int32_t)a >> (immi & 31)) : a >> (immi & 31); break;
        case 6: v = a | (uint32_t)immi; break;
        case 7: v = a & (uint32_t)immi; break;
        }
        break;
    case 0x33:                                                 // op
        if (f7 == 1) {                                         // M extension
            switch (f3) {
            case 0: v = a * b; break;
            case 1: v = mulh((int32_t)a, (int32_t)b); break;
            case 2: v = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(uint64_t)b) >> 32); break;
            case 3: v = (uint32_t)(((uint64_t)a * b) >> 32); break;
            case 4: v = (b == 0) ? 0xFFFFFFFFu : ((a == 0x80000000u && b == 0xFFFFFFFFu)
                        ? a : (uint32_t)((int32_t)a / (int32_t)b)); break;
            case 5: v = (b == 0) ? 0xFFFFFFFFu : a / b; break;
            case 6: v = (b == 0) ? a : ((a == 0x80000000u && b == 0xFFFFFFFFu)
                        ? 0 : (uint32_t)((int32_t)a % (int32_t)b)); break;
            case 7: v = (b == 0) ? a : a % b; break;
            }
            break;
        }
        switch (f3) {
        case 0: v = (f7 & 0x20) ? a - b : a + b; break;
        case 1: v = a << (b & 31); break;
        case 2: v = ((int32_t)a < (int32_t)b); break;
        case 3: v = (a < b); break;
        case 4: v = a ^ b; break;
        case 5: v = (f7 & 0x20) ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
        case 6: v = a | b; break;
        case 7: v = a & b; break;
        }
        break;
    case 0x0F: write = 0; break;                               // fence
    case 0x73:                                                 // system
        if (in == 0x00000073) { ecall(); write = 0; break; }
        if (f3 == 2 && bits(in, 19, 15) == 0) {                // csrr rd, csr
            uint32_t csr = in >> 20;
            if (csr == 0xC00 || csr == 0xC01 || csr == 0xC02) { v = (uint32_t)retired; break; }
            if (csr == 0xC80 || csr == 0xC81 || csr == 0xC82) { v = (uint32_t)(retired >> 32); break; }
        }
        fail("unsupported system instruction", in);
        break;
    default:
        fail("illegal instruction", in);
    }
    if (write && rd) x[rd] = v;
    return next;
}

/* Expands a 16-bit instruction to its 32-bit form. */
static uint32_t expand(uint32_t c) {
    uint32_t q = c & 3, f3 = bits(c, 15, 13);
    uint32_t rdp = 8 + bits(c, 4, 2), rs1p = 8 + bits(c, 9, 7);
    uint32_t rd = bits(c, 11, 7), rs2 = bits(c, 6, 2);
    int32_t imm6 = sext((bits(c, 12, 12) << 5) | bits(c, 6, 2), 6);
#define I_TYPE(op, f, d, s, i)  (((uint32_t)(i) << 20) | ((s) << 15) | ((f) << 12) | ((d) << 7) | (op))
#define R_TYPE(op, f, f7_, d, s, t) (((f7_) << 25) | ((t) << 20) | ((s) << 15) | ((f) << 12) | ((d) << 7) | (op))
#define S_TYPE(f, s, t, i) ((((uint32_t)(i) >> 5) << 25) | ((t) << 20) | ((s) << 15) | ((f) << 12) | (((uint32_t)(i) & 31) << 7) | 0x23)
    if (q == 0) {
        uint32_t uimm = (bits(c, 12, 10) << 3) | (bits(c, 6, 6) << 2) | (bits(c, 5, 5) << 6);
        switch (f3) {
        case 0: {
            uint32_t nz = (bits(c, 12, 11) << 4) | (bits(c, 10, 7) << 6)
                        | (bits(c, 6, 6) << 2) | (bits(c, 5, 5) << 3);
            if (nz == 0) fail("illegal compressed instruction", c);
            return I_TYPE(0x13, 0, rdp, 2, nz);                // c.addi4spn
        }
        case 2: return I_TYPE(0x03, 2, rdp, rs1p, uimm);       // c.lw
        case 6: return S_TYPE(2, rs1p, rdp, uimm);             // c.sw
        }
    } else if (q == 1) {
        uint32_t joff = (bits(c, 12, 12) << 11) | (bits(c, 11, 11) << 4) | (bits(c, 10, 9) << 8)
                      | (bits(c, 8, 8) << 10) | (bits(c, 7, 7) << 6) | (bits(c, 6, 6) << 7)
                      | (bits(c, 5, 3) << 1) | (bits(c, 2, 2) << 5);
        int32_t jimm = sext(joff, 12);
        uint32_t jal = (((uint32_t)jimm >> 20 & 1) << 31) | ((((uint32_t)jimm >> 1) & 0x3FF) << 21)
                     | ((((uint32_t)jimm >> 11) & 1) << 20) | ((((uint32_t)jimm >> 12) & 0xFF) << 12) | 0x6F;
        uint32_t boff = (bits(c, 12, 12) << 8) | (bits(c, 11, 10) << 3) | (bits(c, 6, 5) << 6)
                      | (bits(c, 4, 3) << 1) | (bits(c, 2, 2) << 5);
        uint32_t bimm = (uint32_t)sext(boff, 9);
        uint32_t br = (((bimm >> 12) & 1) << 31) | (((bimm >> 5) & 0x3F) << 25) | (rs1p << 15)
                    | (((bimm >> 1) & 0xF) << 8) | (((bimm >> 11) & 1) << 7) | 0x63;
        switch (f3) {
        case 0: return I_TYPE(0x13, 0, rd, rd, imm6);          // c.addi
        case 1: return jal | (1 << 7);                         // c.jal
        case 2: return I_TYPE(0x13, 0, rd, 0, imm6);           // c.li
        case 3:
            if (rd == 2) {                                     // c.addi16sp
                int32_t i = sext((bits(c, 12, 12) << 9) | (bits(c, 6, 6) << 4) | (bits(c, 5, 5) << 6)
                                 | (bits(c, 4, 3) << 7) | (bits(c, 2, 2) << 5), 10);
                return I_TYPE(0x13, 0, 2, 2, i);
            }
            return ((uint32_t)imm6 << 12) | (rd << 7) | 0x37;  // c.lui
        case 4: {
            uint32_t sh = bits(c, 6, 2) | (bits(c, 12, 12) << 5);
            switch (bits(c, 11, 10)) {
            case 0: return I_TYPE(0x13, 5, rs1p, rs1p, sh);              // c.srli
            case 1: return I_TYPE(0x13, 5, rs1p, rs1p, sh | 0x400);      // c.srai
            case 2: return I_TYPE(0x13, 7, rs1p, rs1p, imm6);            // c.andi
            default:
                if (bits(c, 12, 12)) break;
                switch (bits(c, 6, 5)) {
                case 0: return R_TYPE(0x33, 0, 0x20, rs1p, rs1p, rdp);   // c.sub
                case 1: return R_TYPE(0x33, 4, 0, rs1p, rs1p, rdp);      // c.xor
                case 2: return R_TYPE(0x33, 6, 0, rs1p, rs1p, rdp);      // c.or
                case 3: return R_TYPE(0x33, 7, 0, rs1p, rs1p, rdp);      // c.and
                }
            }
            break;
        }
        case 5: return jal;                                    // c.j
        case 6: return br;                                     // c.beqz
        case 7: return br | (1 << 12);                         // c.bnez
        }
    } else if (q == 2) {
        switch (f3) {
        case 0: return I_TYPE(0x13, 1, rd, rd, bits(c, 6, 2) | (bits(c, 12, 12) << 5));  // c.slli
        case 2: {
            uint32_t u = (bits(c, 12, 12) << 5) | (bits(c, 6, 4) << 2) | (bits(c, 3, 2) << 6);
            return I_TYPE(0x03, 2, rd, 2, u);                  // c.lwsp
        }
        case 4:
            if (!bits(c, 12, 12)) {
                if (rs2 == 0) return I_TYPE(0x67, 0, 0, rd, 0);          // c.jr
                return R_TYPE(0x33, 0, 0, rd, 0, rs2);                   // c.mv
            }
            if (rd == 0 && rs2 == 0) break;                              // c.ebreak
            if (rs2 == 0) return I_TYPE(0x67, 0, 1, rd, 0);              // c.jalr
            return R_TYPE(0x33, 0, 0, rd, rd, rs2);                      // c.add
        case 6: {
            uint32_t u = (bits(c, 12, 9) << 2) | (bits(c, 8, 7) << 6);
            return S_TYPE(2, 2, rs2, u);                       // c.swsp
        }
        }
    }
    fail("illegal compressed instruction", c);
    return 0;
}

static void load_elf(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(99); }
    static uint8_t buf[MEM_SIZE];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 52 || memcmp(buf, "\177ELF\001\001", 6)) fail("not a 32-bit LE ELF", 0);
#define U16(o) ((uint32_t)buf[o] | ((uint32_t)buf[(o) + 1] << 8))
#define U32(o) (U16(o) | (U16((o) + 2) << 16))
    pc = U32(24);
    uint32_t phoff = U32(28), phentsize = U16(42), phnum = U16(44);
    for (uint32_t i = 0; i < phnum; i++) {
        uint32_t ph = phoff + i * phentsize;
        if (U32(ph) != 1) continue;                            // PT_LOAD
        uint32_t off = U32(ph + 4), vaddr = U32(ph + 8);
        uint32_t filesz = U32(ph + 16), memsz = U32(ph + 20);
        if (memsz) at(vaddr, 1), at(vaddr + memsz - 1, 1);
        memcpy(mem + vaddr, buf + off, filesz);
        memset(mem + vaddr + filesz, 0, memsz - filesz);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: rv32emu program.elf\n");
        return 2;
    }
    mem = calloc(MEM_SIZE, 1);
    load_elf(argv[1]);
    x[2] = MEM_SIZE - 16;                                      // sp
    for (;;) {
        uint32_t in = load(pc, 2, 0);
        if ((in & 3) == 3) {
            ilen = 4;
            pc = exec32(in | (load(pc + 2, 2, 0) << 16));
        } else {
            ilen = 2;
            pc = exec32(expand(in));
        }
        retired++;
    }
}
