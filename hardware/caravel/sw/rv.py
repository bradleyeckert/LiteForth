"""Tiny RV32I assembler helpers + reference ISS (RV32E register file) for mcu_top."""
M = 0xFFFFFFFF

def _r(f7, rs2, rs1, f3, rd, op): return (f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op
def _i(imm, rs1, f3, rd, op): return ((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op
def _s(imm, rs2, rs1, f3, op):
    imm &= 0xFFF
    return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | ((imm & 0x1F) << 7) | op
def _b(imm, rs2, rs1, f3):
    imm &= 0x1FFF
    return (((imm >> 12) & 1) << 31) | (((imm >> 5) & 0x3F) << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | \
           (((imm >> 1) & 0xF) << 8) | (((imm >> 11) & 1) << 7) | 0x63
def _u(imm, rd, op): return (imm & 0xFFFFF000) | (rd << 7) | op
def _j(imm, rd):
    imm &= 0x1FFFFF
    return (((imm >> 20) & 1) << 31) | (((imm >> 1) & 0x3FF) << 21) | (((imm >> 11) & 1) << 20) | \
           (((imm >> 12) & 0xFF) << 12) | (rd << 7) | 0x6F

RALU = {'add': (0, 0), 'sub': (0x20, 0), 'sll': (0, 1), 'slt': (0, 2), 'sltu': (0, 3),
        'xor': (0, 4), 'srl': (0, 5), 'sra': (0x20, 5), 'or': (0, 6), 'and': (0, 7)}
IALU = {'addi': 0, 'slti': 2, 'sltiu': 3, 'xori': 4, 'ori': 6, 'andi': 7}
SHI = {'slli': (0, 1), 'srli': (0, 5), 'srai': (0x20, 5)}
LD = {'lb': 0, 'lh': 1, 'lw': 2, 'lbu': 4, 'lhu': 5}
ST = {'sb': 0, 'sh': 1, 'sw': 2}
BR = {'beq': 0, 'bne': 1, 'blt': 4, 'bge': 5, 'bltu': 6, 'bgeu': 7}


class Asm:
    """Two-pass-free assembler: labels resolved at link() time."""
    def __init__(self):
        self.items = []  # (kind, args)
        self.labels = {}

    def pc(self): return 4 * len(self.items)
    def label(self, name): self.labels[name] = self.pc()
    def emit(self, word): self.items.append(('w', word))

    def r(self, op, rd, rs1, rs2): f7, f3 = RALU[op]; self.emit(_r(f7, rs2, rs1, f3, rd, 0x33))
    def i(self, op, rd, rs1, imm):
        if op in SHI: f7, f3 = SHI[op]; self.emit(_i((f7 << 5) | (imm & 31), rs1, f3, rd, 0x13))
        else: self.emit(_i(imm, rs1, IALU[op], rd, 0x13))
    def ld(self, op, rd, rs1, imm): self.emit(_i(imm, rs1, LD[op], rd, 0x03))
    def st(self, op, rs2, rs1, imm): self.emit(_s(imm, rs2, rs1, ST[op], 0x23))
    def lui(self, rd, imm): self.emit(_u(imm, rd, 0x37))
    def auipc(self, rd, imm): self.emit(_u(imm, rd, 0x17))
    def jalr(self, rd, rs1, imm): self.emit(_i(imm, rs1, 0, rd, 0x67))
    def ecall(self): self.emit(0x00000073)
    def li(self, rd, val):
        val &= M
        lo = val & 0xFFF
        if lo & 0x800: lo -= 0x1000
        hi = (val - lo) & M
        if hi: self.lui(rd, hi); (self.i('addi', rd, rd, lo) if lo else None)
        else: self.i('addi', rd, 0, lo)
    # label-relative
    def br(self, op, rs1, rs2, lbl): self.items.append(('b', (op, rs1, rs2, lbl, self.pc())))
    def jal(self, rd, lbl): self.items.append(('j', (rd, lbl, self.pc())))

    def link(self):
        out = []
        for k, a in self.items:
            if k == 'w': out.append(a)
            elif k == 'b':
                op, rs1, rs2, lbl, pc = a; out.append(_b(self.labels[lbl] - pc, rs2, rs1, BR[op]))
            else:
                rd, lbl, pc = a; out.append(_j(self.labels[lbl] - pc, rd))
        return out


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


class ISS:
    """Reference model. Memory-mapped IO above 0x8000_0000 is ignored (reads 0)."""
    def __init__(self, words, ram_bytes):
        self.mem = bytearray(ram_bytes)
        for n, w in enumerate(words): self.mem[4*n:4*n+4] = w.to_bytes(4, 'little')
        self.x = [0] * 16
        self.pc = 0
        self.mmio = []

    def rd32(self, a): return int.from_bytes(self.mem[a:a+4], 'little')

    def run(self, max_steps=100000):
        for _ in range(max_steps):
            ins = self.rd32(self.pc)
            op = ins & 0x7F; rd = (ins >> 7) & 0xF; f3 = (ins >> 12) & 7
            rs1 = (ins >> 15) & 0xF; rs2 = (ins >> 20) & 0xF; f7 = ins >> 25
            a, b = self.x[rs1], self.x[rs2]
            ii = sx(ins >> 20, 12)
            npc = (self.pc + 4) & M
            res = None
            if op == 0x37: res = ins & 0xFFFFF000
            elif op == 0x17: res = (self.pc + (ins & 0xFFFFF000)) & M
            elif op == 0x6F:
                imm = sx((((ins >> 31) & 1) << 20) | (((ins >> 12) & 0xFF) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3FF) << 1), 21)
                res = npc; npc = (self.pc + imm) & M
            elif op == 0x67: res = npc; npc = (a + ii) & M & ~1
            elif op == 0x63:
                imm = sx((((ins >> 31) & 1) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3F) << 5) | (((ins >> 8) & 0xF) << 1), 13)
                sa, sb = sx(a, 32), sx(b, 32)
                t = {0: a == b, 1: a != b, 4: sa < sb, 5: sa >= sb, 6: a < b, 7: a >= b}[f3]
                if t: npc = (self.pc + imm) & M
            elif op == 0x03:
                ad = (a + ii) & M
                if ad & 0x80000000: v = 0; self.mmio.append(('r', ad))
                else:
                    v = self.rd32(ad & ~3) >> (8 * (ad & 3))
                res = {0: sx(v, 8) & M, 1: sx(v, 16) & M, 2: v & M, 4: v & 0xFF, 5: v & 0xFFFF}[f3]
            elif op == 0x23:
                ad = (a + sx(((ins >> 25) << 5) | ((ins >> 7) & 0x1F), 12)) & M
                if ad & 0x80000000: self.mmio.append(('w', ad, b))
                else:
                    n = {0: 1, 1: 2, 2: 4}[f3]
                    self.mem[ad:ad+n] = (b & ((1 << (8*n)) - 1)).to_bytes(n, 'little')
            elif op in (0x13, 0x33):
                bb = b if op == 0x33 else ii & M
                sh = bb & 31
                if f3 == 0: res = (a - bb) if (op == 0x33 and f7 == 0x20) else (a + bb)
                elif f3 == 1: res = a << sh
                elif f3 == 2: res = int(sx(a, 32) < sx(bb, 32))
                elif f3 == 3: res = int(a < (bb & M))
                elif f3 == 4: res = a ^ bb
                elif f3 == 5: res = (sx(a, 32) >> sh) if (ins >> 30) & 1 else (a >> sh)
                elif f3 == 6: res = a | bb
                else: res = a & bb
                res &= M
            elif op == 0x0F: pass
            else:
                return True  # ecall / illegal -> halt
            if res is not None and rd: self.x[rd] = res & M
            self.pc = npc
        return False
