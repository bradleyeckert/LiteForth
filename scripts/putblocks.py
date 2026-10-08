#!/usr/bin/env python3
"""Write Forth source into LiteForth block storage.

Usage: putblocks.py [-n] source.f [blockfile]

The source is split at lines that start with `( BLOCK n )`: the lines after
it, up to the next marker, become block n. The marker line itself (all of
it) isn't written; getblocks.py makes the markers again. `( BLOCK n +)`
also puts `-->` at the end of the block (the last 3 columns of its last
row), so loading block n goes on to block n+1; such a block holds at most
31 lines. A block is 4096
bytes shown as 32 rows of 128 columns (SCREEN_COLUMNS), so each line is
padded with spaces to 128 bytes, and the rows after the last line are
spaces. `//` comments rely on this: they skip to the next 128-byte row.
Lines are counted in UTF-8 bytes, as LiteForth sees them.

A line that starts with `( BLOCK IGNORE )` ends the current block; the
lines after it, up to the next `( BLOCK n )`, aren't written anywhere. Use
it for notes, or to keep code in the source without putting it in a block.
Text before the first marker is ignored the same way. Either gives a note
with the number of non-blank lines skipped. A line longer than
128 bytes, more than 32 lines in a block, or the same block twice is an
error (31 lines with `+`), and nothing is written.

The block file defaults to lfblocks.fb4 in the current directory, as for
`lf`. It must already exist and start with the signature LITEFORTH, the way
lf requires before it writes a block, and block 0 (which holds the
signature) is never written. A block past the end of a regular file
extends the file with blank (space) blocks; a device, such as a partition
(/dev/sdb2), is never extended.

It lists each block it writes (n+ for one that ends with -->) with its line
count and its first line, which by convention is a comment saying what the
block holds.

-n  only show what would be written.
"""
import os
import re
import stat
import sys

BLOCK_BYTES = 4096
COLUMNS = 128
ROWS = BLOCK_BYTES // COLUMNS           # 32
SIGNATURE = b"LITEFORTH"
CHAIN_ROW = b"-->".rjust(COLUMNS, b" ")  # the last row of a `+` block
MARKER = re.compile(rb"^\(\s*BLOCK\s+(\d+)\s*(\+?)\s*\)")
IGNORE = re.compile(rb"^\(\s*BLOCK\s+IGNORE\s*\)")


class SourceError(Exception):
    pass


def parse(path):
    """Returns ({block number: [line bytes, ...]}, {numbers of the blocks
    that end with -->}) from the source file."""
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"\xef\xbb\xbf"):            # UTF-8 BOM
        data = data[3:]
    blocks = {}
    chained = set()
    current = None
    skipped = 0
    errors = []
    for lineno, line in enumerate(data.splitlines(), 1):
        line = line.rstrip(b"\r")
        m = MARKER.match(line)
        if m:
            current = int(m.group(1))
            if current in blocks:
                errors.append(f"{path}:{lineno}: block {current} appears twice")
            blocks[current] = []
            if m.group(2):
                chained.add(current)
            continue                            # the marker isn't stored
        if IGNORE.match(line):
            current = None                      # skip to the next marker
            continue
        if current is None:
            if line.strip():
                skipped += 1
            continue
        if len(line) > COLUMNS:
            errors.append(f"{path}:{lineno}: line is {len(line)} bytes, "
                          f"more than {COLUMNS}")
        blocks[current].append(line)
        rows = ROWS - 1 if current in chained else ROWS
        if len(blocks[current]) == rows + 1:
            errors.append(f"{path}:{lineno}: block {current} has more than "
                          f"{rows} lines" + (" (the last row holds -->)"
                                             if current in chained else ""))
    if skipped:
        print(f"note: {skipped} non-blank line(s) outside blocks ignored "
              f"(before the first `( BLOCK n )` or after `( BLOCK IGNORE )`)",
              file=sys.stderr)
    if 0 in blocks:
        errors.append(f"{path}: block 0 holds the signature and isn't written")
    if errors:
        raise SourceError("\n".join(errors))
    return blocks, chained


def render(lines, chain=False):
    """Returns the 4096-byte image of a block, ending with --> if chain."""
    rows = [line.ljust(COLUMNS, b" ") for line in lines]
    rows += [b" " * COLUMNS] * (ROWS - len(rows))
    if chain:
        rows[-1] = CHAIN_ROW
    return b"".join(rows)


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
    if not 1 <= len(args) <= 2:
        print("usage: putblocks.py [-n] source.f [blockfile]", file=sys.stderr)
        return 2
    source = args[0]
    target = args[1] if len(args) > 1 else "lfblocks.fb4"

    try:
        blocks, chained = parse(source)
    except (SourceError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if not blocks:
        print(f"error: no `( BLOCK n )` markers in {source}", file=sys.stderr)
        return 1

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
        last = max(blocks)
        if last >= capacity and not regular:
            print(f"error: block {last} is past the end of {target} "
                  f"({capacity} blocks)", file=sys.stderr)
            return 1
        for n in sorted(blocks):
            lines = blocks[n]
            count = f"{len(lines)} line{'' if len(lines) == 1 else 's'}"
            title = " ".join(lines[0].decode("utf-8", "replace").split()) if lines else ""
            label = f"{n}+" if n in chained else str(n)
            print(f"block {label:<4} {count:<9} {title}".rstrip())
        if dry:
            print(f"(-n: {target} not changed)")
            return 0
        if last >= capacity:                    # extend a file with blanks
            f.seek(capacity * BLOCK_BYTES)
            f.write(b" " * ((last + 1 - capacity) * BLOCK_BYTES))
            print(f"{target}: extended from {capacity} to {last + 1} blocks")
        for n in sorted(blocks):
            f.seek(n * BLOCK_BYTES)
            f.write(render(blocks[n], n in chained))
        f.flush()
        os.fsync(f.fileno())
    print(f"{target}: wrote {len(blocks)} block(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
