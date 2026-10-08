#!/bin/sh
# scripts/putblocks.py and getblocks.py test: writes a source with `( BLOCK n )` markers into
# a fresh 64-block file, has bin/lf load the blocks, dumps them
# with getblocks.py and writes the dump back to get the same blocks, and
# checks that bad sources (line too long, block 0) change nothing. Compares
# the output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
PUT="$ROOT/scripts/putblocks.py"
if ! command -v python3 > /dev/null 2>&1; then
    echo "putblocks tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfflash.bin" .
blank() {  # a signed block file of 64 blank blocks
    { printf 'LITEFORTH'; head -c $((64 * 4096 - 9)) /dev/zero | tr '\000' ' '; } > "$1"
}
blank lfblocks.fb4

{
cat > src.f <<'F'
text before the first marker is ignored
( BLOCK 3 )
.( three ) cr  ( a comment )
.( three again ) cr
( BLOCK 70 )
.( seventy, past the end of the file ) cr
F
python3 "$PUT" src.f 2>&1
printf 'capacity .\n3 load\n70 load\nbye\n' | "$LF" -o 3 2>&1 | tail -n +2

# getblocks.py dumps the blocks as text; putblocks.py writes them back the same
python3 "$ROOT/scripts/getblocks.py" -o dump.f lfblocks.fb4
blank again.fb4
python3 "$PUT" dump.f again.fb4 > /dev/null 2>&1
dd if=again.fb4 of=again71.fb4 bs=4096 count=71 2>/dev/null
cmp -s lfblocks.fb4 again71.fb4 && echo "round trip: same blocks"
grep -c '^( BLOCK \(3\|70\) )' dump.f     # one marker per block

cp lfblocks.fb4 before.bin
python3 -c "print('( BLOCK 4 )'); print('x' * 129)" > long.f
python3 "$PUT" long.f 2>&1 || echo "(exit $?)"
printf '( BLOCK 0 )\n' > zero.f
python3 "$PUT" zero.f 2>&1 || echo "(exit $?)"
cmp -s before.bin lfblocks.fb4 && echo unchanged
} > got.txt 2>&1 || true

# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "putblocks tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "putblocks tests FAILED (output saved as got.txt)"
    exit 1
fi
