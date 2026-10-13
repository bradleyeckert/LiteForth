#!/usr/bin/env python3
"""Write a binary file, such as a font from mkfont.py, into LiteForth blocks.

Usage: putbin.py [-n] file.bin block [blockfile]

The file goes in as one stream starting at byte 32 of `block` (OFFSET),
running on through the blocks after it. The first 32 bytes of that block
are left for an HMAC of the data and are written as zeros, as is the rest
of the last block. So byte o of the file is at byte (o + 32) mod 4096 of
block `block + (o + 32) / 4096`. This is the layout mkfont.py --blocks
expects; a font built without --blocks still works, but some of its glyphs
cross block boundaries, and putbin.py says how many.

The block file defaults to lfblocks.fb4 in the current directory, as for
`lf`. It must already exist and start with the signature LITEFORTH, and
block 0 is never written, as with putblocks.py. A block past the end of a
regular file extends the file with blank (space) blocks; a device, such as
a partition (/dev/sdb2), is never extended.

It prints the blocks it wrote and the next free block, which is where the
next file can go:
    putbin.py cjk24.lfnt 100       -> blocks 100-127, next free block 128
    putbin.py cjk16.lfnt 128

-n  only show what would be written.
"""
import os
import stat
import struct
import sys

BLOCK_BYTES = 4096
OFFSET = 32                     # bytes kept for an HMAC at the start
SIGNATURE = b"LITEFORTH"


def image(data):
    """Returns the blocks' bytes: 32 zeros, the data, zeros to a block end."""
    out = bytes(OFFSET) + data
    return out + bytes(-len(out) % BLOCK_BYTES)


def font_crossings(data):
    """For a .lfnt font, returns how many glyph records cross a block
    boundary in this layout, or None if data isn't a font."""
    if len(data) < 528 or data[:4] != b"LFNT":
        return None
    n, = struct.unpack_from("<H", data, 8)
    crossing = 0
    for i in range(n):
        e = struct.unpack_from("<I", data, 528 + 4 * i)[0] & 0xFFFFFF
        w, h = data[e], data[e + 1]
        j, px = e + 6, 0
        while px < w * h and j + 2 <= len(data):    # the codes, to the record's end
            c, = struct.unpack_from("<H", data, j)
            j += 2
            px += (2 + ((c >> 7) & 7) + (c & 7), 1 + (c & 511),
                   2 + (c & 31), 3 + (c & 3))[c >> 14]
        if (e + OFFSET) // BLOCK_BYTES != (j - 1 + OFFSET) // BLOCK_BYTES:
            crossing += 1
    return crossing


def main(argv):
    dry = False
    args = []
    for a in argv:
        if a == "-n":
            dry = True
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            args.append(a)
    if not 2 <= len(args) <= 3:
        print("usage: putbin.py [-n] file.bin block [blockfile]", file=sys.stderr)
        return 2
    source = args[0]
    try:
        first = int(args[1], 0)
    except ValueError:
        print(f"error: the block number {args[1]} isn't a number", file=sys.stderr)
        return 2
    target = args[2] if len(args) > 2 else "lfblocks.fb4"
    if first < 1:
        print("error: block 0 holds the signature and isn't written", file=sys.stderr)
        return 1
    try:
        with open(source, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if not data:
        print(f"error: {source} is empty", file=sys.stderr)
        return 1
    img = image(data)
    count = len(img) // BLOCK_BYTES
    last = first + count - 1

    try:
        f = open(target, "rb" if dry else "r+b")
    except OSError as e:
        print(f"error: can't open {target}: {e}", file=sys.stderr)
        return 1
    with f:
        if f.read(len(SIGNATURE)) != SIGNATURE:
            print(f"error: {target} doesn't start with {SIGNATURE.decode()}, "
                  f"so it isn't a LiteForth block file", file=sys.stderr)
            return 1
        regular = stat.S_ISREG(os.fstat(f.fileno()).st_mode)
        f.seek(0, os.SEEK_END)
        capacity = f.tell() // BLOCK_BYTES
        if last >= capacity and not regular:
            print(f"error: block {last} is past the end of {target} "
                  f"({capacity} blocks)", file=sys.stderr)
            return 1
        span = f"block {first}" if count == 1 else f"blocks {first}-{last}"
        print(f"{source}: {len(data)} bytes at byte {OFFSET} of block {first}: "
              f"{span}, next free block {last + 1}")
        crossing = font_crossings(data)
        if crossing:
            print(f"note: {crossing} glyph(s) of this font cross a block boundary; "
                  f"build it with mkfont.py --blocks so none do")
        if dry:
            print(f"(-n: {target} not changed)")
            return 0
        if last >= capacity:                    # extend a file with blanks
            f.seek(capacity * BLOCK_BYTES)
            f.write(b" " * ((last + 1 - capacity) * BLOCK_BYTES))
            print(f"{target}: extended from {capacity} to {last + 1} blocks")
        f.seek(first * BLOCK_BYTES)
        f.write(img)
        f.flush()
        os.fsync(f.fileno())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
