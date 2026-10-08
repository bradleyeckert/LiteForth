#!/bin/sh
# Block file signature test: a block file that starts with LITEFORTH can be
# read and written; one that doesn't offers only block 0, read-only, and is
# never modified. Runs bin/lf on scratch block files and compares its output
# with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$HERE/../../../../../bin/lfflash.bin" .

run() {  # run "label": the label, then lf's output without its banner
    echo "[$1]"
    "$LF" -o 3 2>&1 | tail -n +2
    echo
}

{
# lf creates a blank, signed file of SIMNUMBLOCKS blocks
printf 'capacity .\nbye\n' | run fresh
head -c 9 lfblocks.fb4; echo

# a signed file is padded to whole blocks, and every block can be written
printf 'LITEFORTH' > lfblocks.fb4
printf '%-4991s' '' >> lfblocks.fb4              # 5000 bytes in all
printf 'capacity .\n1 block drop update flush .( wrote-1 )\nbye\n' | run signed
wc -c < lfblocks.fb4 | tr -d ' '

# without the signature: capacity 1, block 0 readable, nothing writable
printf 'not a LiteForth file' > lfblocks.fb4
printf '%-8172s' '' >> lfblocks.fb4              # 8192 bytes, 2 blocks
cp lfblocks.fb4 before.bin
printf 'capacity .\n0 block drop .( read-0 )\n1 block drop\n0 block drop update flush\nbye\n' | run foreign
cmp -s before.bin lfblocks.fb4 && echo untouched

# without the signature and shorter than a block: nothing to read
printf 'short' > lfblocks.fb4
printf 'capacity .\nbye\n' | run short
} > got.txt 2>&1 || true
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "blocks tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "blocks tests FAILED (output saved as got.txt)"
    exit 1
fi
