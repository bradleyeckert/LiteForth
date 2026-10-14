"""Helper for run.sh: `make blockfile blk ...` writes the Forth test to
stdout and the expected lines to expected.txt; `compare out.txt flash`
checks lf's output against them and the cache tag in flash page 3."""
import random
import struct
import sys

B = 4096


def font(data, blk):
    f = data[blk * B + 32:]
    if f[:4] != b"LFNT":
        return None
    return f


def lookup(f, cp):
    lo, hi = struct.unpack_from("<HH", f, 12 + 2 * (cp >> 8))
    while lo < hi:
        mid = (lo + hi) // 2
        e, = struct.unpack_from("<I", f, 528 + 4 * mid)
        if e >> 24 == cp & 255:
            return e & 0xFFFFFF
        lo, hi = (mid + 1, hi) if e >> 24 < cp & 255 else (lo, mid)
    return None


def make(path, blks):
    data = open(path, "rb").read()
    cases, want = [], []
    for blk in blks:
        f = font(data, blk)
        if f is None:
            sys.exit(1)
        cps = []
        for p in range(256):
            lo, hi = struct.unpack_from("<HH", f, 12 + 2 * p)
            cps += [(p << 8) | (struct.unpack_from("<I", f, 528 + 4 * i)[0] >> 24)
                    for i in range(lo, hi)]
        rnd = random.Random(blk)
        miss = [c for c in (rnd.randrange(65536) for _ in range(100))
                if lookup(f, c) is None] + [0, 65535]
        have = set(cps)
        for p in sorted({c >> 8 for c in cps}):     # misses that make the
            lows = sorted(c & 255 for c in cps if c >> 8 == p)  # search move
            gaps = [(a + b) // 2 for a, b in zip(lows, lows[1:]) if b - a > 1][:8]
            miss += [(p << 8) | x for x in [0, 255] + gaps if (p << 8) | x not in have]
        for cp in cps + miss:
            o = lookup(f, cp)
            cases.append((cp, blk))
            want.append("0" if o is None
                        else " ".join(map(str, struct.unpack_from("<4H", f, o))))
    with open("expected.txt", "w") as e:
        e.write("\n".join(want) + "\n")
    out = ["3 >options", "0 open-flash",
           ": .g  ( u blk -- )  glyph dup 0= if . cr exit then  4 for @+ u. next drop cr ;",
           "close-flash"]
    for cache in [None] + list(blks):
        out.append(".( --- ) cr" if cache is None else f"{cache} font-cache .( --- ) cr")
        out += [f"{cp} {blk} .g" for cp, blk in cases]
    out.append(".( --- ) cr bye")
    print("\n".join(out))


def compare(out, flash):
    want = open("expected.txt").read().split("\n")[:-1]
    got = open(out, encoding="utf-8", errors="replace").read().split("\n")
    marks = [i for i, line in enumerate(got) if line.rstrip().endswith("---")]
    if len(marks) < 2:
        print("".join(got[-20:]))
        return 1
    bad = 0
    for k in range(len(marks) - 1):
        part = [line.strip() for line in got[marks[k] + 1:marks[k + 1]]]
        if part != want:
            for i, (g, w) in enumerate(zip(part + [""] * len(want), want)):
                if g != w:
                    print(f"pass {k}, case {i}: got {g!r}, want {w!r}")
                    break
            bad += 1
    tag = struct.unpack_from("<2I", open(flash, "rb").read(), 3 * 65536)
    if tag[0] != 120 or tag[1] % B or not 0 < tag[1] <= 16 * B:
        print(f"page 3 tag {tag}")
        bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "make":
        make(sys.argv[2], [int(b) for b in sys.argv[3:]])
    else:
        sys.exit(compare(sys.argv[2], sys.argv[3]))
