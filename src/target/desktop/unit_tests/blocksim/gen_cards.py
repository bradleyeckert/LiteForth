#!/usr/bin/env python3
"""Makes blocksim's two card images with scripts/mkexfat.py (MBR, exFAT):

  card.img      lfblocks.fb4: 40 blocks, block 0 the LITEFORTH signature,
                block n >= 1 byte i = (37n + 7i + i // 512) & 0xFF (sim.c's
                pattern)
  unsigned.img  the same file without the signature (capacity 1, read-only)

usage: gen_cards.py OUTDIR
"""
import os
import subprocess
import sys

BLOCKS = 40
BLOCK = 4096


def block_file(signed):
    out = bytearray()
    head = (b"LITEFORTH" if signed else b"NOTFORTH!").ljust(BLOCK, b"\0")
    out += head
    for n in range(1, BLOCKS):
        out += bytes((n * 37 + i * 7 + (i >> 9)) & 0xFF for i in range(BLOCK))
    return bytes(out)


def main():
    outdir = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    mkexfat = os.path.join(here, "..", "..", "..", "..", "..", "scripts", "mkexfat.py")
    for name, signed in (("card", True), ("unsigned", False)):
        src = os.path.join(outdir, name + ".fb4")
        with open(src, "wb") as f:
            f.write(block_file(signed))
        subprocess.run([sys.executable, mkexfat, "--size-mb", "8",
                        "--file", "lfblocks.fb4=" + src, os.path.join(outdir, name + ".img")],
                       check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
