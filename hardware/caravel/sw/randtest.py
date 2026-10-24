"""Random instruction test: same program on RTL (iverilog) and Python ISS, compare RAM."""
import random, subprocess, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from rv import Asm, ISS, RALU, IALU, SHI, LD, ST, BR, _b, _j

WORDS = 1024
DATA = 0x800

def gen(seed, n=300):
    rnd = random.Random(seed)
    a = Asm()
    nol = set()
    a.li(15, DATA)
    for r in range(1, 15):
        a.li(r, rnd.choice([rnd.getrandbits(32), rnd.randint(-5, 5), 0x80000000, 0x7fffffff, rnd.getrandbits(8)]))
    # seed data area
    for k in range(0, 64, 4):
        a.st('sw', rnd.randint(1, 14), 15, k)
    rr = lambda: rnd.randint(0, 15)
    rdr = lambda: rnd.randint(1, 14)
    for _ in range(n):
        c = rnd.random()
        if c < 0.25: a.r(rnd.choice(list(RALU)), rdr(), rr(), rr())
        elif c < 0.45: a.i(rnd.choice(list(IALU)), rdr(), rr(), rnd.randint(-2048, 2047))
        elif c < 0.52: a.i(rnd.choice(list(SHI)), rdr(), rr(), rnd.randint(0, 31))
        elif c < 0.56: a.lui(rdr(), rnd.getrandbits(32))
        elif c < 0.58: a.auipc(rdr(), rnd.getrandbits(32))
        elif c < 0.68:
            op = rnd.choice(list(LD)); al = {'lb': 1, 'lbu': 1, 'lh': 2, 'lhu': 2, 'lw': 4}[op]
            a.ld(op, rdr(), 15, rnd.randrange(0, 64, al))
        elif c < 0.78:
            op = rnd.choice(list(ST)); al = {'sb': 1, 'sh': 2, 'sw': 4}[op]
            a.st(op, rr(), 15, rnd.randrange(0, 64, al))
        elif c < 0.92:
            a.items.append(('B', (rnd.randint(1, 3), rr(), rr(), BR[rnd.choice(list(BR))])))
        elif c < 0.96:
            a.items.append(('J', rdr()))   # jal over one instruction
        else:
            ra, rb = rdr(), rdr()
            a.auipc(ra, 0); nol.add(len(a.items)); a.jalr(rb, ra, 12)   # skips one instr after jalr
            a.i('xori', rdr(), rr(), rnd.randint(-2048, 2047))         # (skipped filler)
    for r in range(1, 16):
        a.st('sw', r, 15, 0x200 + 4 * r - 4) if r != 15 else a.st('sw', 15, 15, 0x200 + 56)
    a.ecall()
    for k, (kind, arg) in enumerate(a.items):   # resolve branches, never land on a jalr
        if kind == 'B':
            skip, r1, r2, f3 = arg
            while k + skip + 1 in nol: skip += 1
            a.items[k] = ('w', _b(4 * (skip + 1), r2, r1, f3))
        elif kind == 'J':
            skip = 1
            while k + skip + 1 in nol: skip += 1
            a.items[k] = ('w', _j(4 * (skip + 1), arg))
    return a.link()

def run(seed, n=300, vvp_bin='build/tb_rtl.vvp', simdir='build'):
    prog = gen(seed, n)
    assert len(prog) < DATA // 4, len(prog)
    hexf = f'{simdir}/rand_{seed}.hex'; dump = f'{simdir}/rand_{seed}.dump'
    with open(hexf, 'w') as f: f.write('\n'.join(f'{w:08x}' for w in prog) + '\n')
    iss = ISS(prog, WORDS * 4)
    assert iss.run(), 'ISS did not halt'
    out = subprocess.run(['vvp', '-n', vvp_bin, f'+HEX={hexf}', f'+DUMP={dump}'], capture_output=True, text=True).stdout
    got = [int(l, 16) for l in open(dump) if l.strip()]
    exp = [iss.rd32(4 * i) for i in range(WORDS)]
    bad = [(i, got[i], exp[i]) for i in range(WORDS) if got[i] != exp[i]]
    os.remove(hexf); os.remove(dump)
    if bad or 'ERROR' in out:
        print(f'seed {seed}: FAIL', out, [(hex(4*i), hex(g), hex(e)) for i, g, e in bad[:10]])
        return False
    return True

if __name__ == '__main__':
    n0, n1 = int(sys.argv[1]), int(sys.argv[2])
    fails = sum(not run(s) for s in range(n0, n1))
    print(f'{n1 - n0} random programs, {fails} failures')
