#!/usr/bin/env python3
"""Dump LiteForth block storage as text: the reverse of putblocks.py.

Usage: getblocks.py [-0] [-o out.f] [blockfile]

Each block that isn't blank becomes a `( BLOCK n )` marker line followed by
its 32 rows of 128 columns as lines, trailing spaces and trailing empty rows
removed. putblocks.py drops the markers, so `putblocks.py out.f` writes the
same blocks back. A block whose first row is its own `( BLOCK n )` (written
by an older putblocks.py, which kept the marker) shows that row as the
marker, with a note on stderr.

Block 0 holds the file's signature and header and is skipped, unless -0 is
given: then its text comes first, before any marker, where putblocks.py
ignores it. A control character in a row (which a line can't carry) is
shown as `?`, with a warning on stderr (except in block 0, whose header
ends with a newline).

The block file defaults to lfblocks.fb4; the text goes to stdout, or to
the file given with -o.

It is also a git diff driver, so `git diff` shows block files as text:
    .gitattributes:  *.fb4 diff=fb4
    git config diff.fb4.textconv "python3 scripts/getblocks.py -0"
(use `py` instead of `python3` if that's how Python runs on Windows).
"""
import re
import sys

BLOCK_BYTES = 4096
COLUMNS = 128
MARKER = re.compile(rb"^\(\s*BLOCK\s+(\d+)\s*\)")
ANY_MARKER = re.compile(rb"^\(\s*BLOCK\s+(\d+|IGNORE)\s*\)")


def block_lines(block, n):
    """Returns the text lines of one block, or None if it is blank."""
    if not block.strip(b" \0"):
        return None
    rows = []
    bad = 0
    for i in range(0, len(block), COLUMNS):
        row = block[i:i + COLUMNS].rstrip(b" ")
        clean = bytes(c if c >= 0x20 or c == 0x09 else 0x3F for c in row)
        if clean != row:
            bad += 1
        rows.append(clean)
    while rows and not rows[-1]:
        rows.pop()
    if bad and n != 0:          # block 0's header has a newline in it
        print(f"warning: block {n}: {bad} row(s) with control characters, "
              f"shown as ?", file=sys.stderr)
    return rows


def dump(data, with_header):
    out = []
    old_style = []
    count = len(data) // BLOCK_BYTES
    for n in range(0 if with_header else 1, count):
        lines = block_lines(data[n * BLOCK_BYTES:(n + 1) * BLOCK_BYTES], n)
        if lines is None:
            continue
        if n == 0:
            if with_header:
                out.append(b"( block 0: the header, not written by putblocks.py )")
                out.extend(lines)
                out.append(b"")
            continue
        m = MARKER.match(lines[0])
        if m and int(m.group(1)) == n:          # written by an older putblocks.py
            lines = lines[1:]
            old_style.append(n)
        for row in lines:
            if ANY_MARKER.match(row):
                print(f"warning: block {n} has a row that starts with "
                      f"`( BLOCK`; putblocks.py would end the block there",
                      file=sys.stderr)
        out.append(b"( BLOCK %d )" % n)
        out.extend(lines)
    if old_style:
        print(f"note: block(s) {', '.join(map(str, old_style))} start with their "
              f"own `( BLOCK n )` row (an older putblocks.py); it is shown as "
              f"the marker, so writing the dump back moves their rows up one",
              file=sys.stderr)
    return b"\n".join(out) + (b"\n" if out else b"")


def main(argv):
    with_header = False
    out_path = None
    args = []
    it = iter(argv)
    for a in it:
        if a == "-0":
            with_header = True
        elif a == "-o":
            out_path = next(it, None)
            if out_path is None:
                print("error: -o needs a file name", file=sys.stderr)
                return 2
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            args.append(a)
    if len(args) > 1:
        print("usage: getblocks.py [-0] [-o out.f] [blockfile]", file=sys.stderr)
        return 2
    source = args[0] if args else "lfblocks.fb4"
    try:
        with open(source, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"error: can't read {source}: {e}", file=sys.stderr)
        return 1
    text = dump(data, with_header)
    if out_path:
        with open(out_path, "wb") as f:
            f.write(text)
    else:
        sys.stdout.buffer.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
