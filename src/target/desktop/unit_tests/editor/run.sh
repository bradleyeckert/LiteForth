#!/bin/sh
# Test of the block editor (forth/core.f, blocks 18 to 20): loads core.f into
# bin/lf with a scratch block file, runs an editing session on block 40, then
# dumps the block with getblocks.py to check that the edits were saved.
# core.f ends with `0 >options`, so the session first turns the prompt and
# stack display off again. Compares the output from the `.( --- )` marker on
# (CRs removed) with expected.txt.
# Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
if ! command -v python3 > /dev/null 2>&1; then
    echo "editor tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
{ printf 'LITEFORTH'; head -c $((64 * 4096 - 9)) /dev/zero | tr '\000' ' '; } > lfblocks.fb4
printf '( BLOCK 40 )\n( test block )\n: square dup * ;\nline two\n' > b40.f
python3 "$ROOT/scripts/putblocks.py" b40.f > /dev/null

{
cat "$ROOT/forth/core.f"
cat <<'F'
3 >options  .( --- ) cr
editor
40 edit
1 t
p : cube dup dup * * ;
0 t
i ( inserted above line 0 )
f dup
c DUP
f
2 x
3 s
r replaced tail
e
3 d
f zzz
c nothing
31 t
i pushed off
l
41 edit
p xyz
clear
l
forth order cr
bye
F
} | "$LF" -o 3 > out.txt 2>&1 || true
tr -d '\r' < out.txt | sed -n '/---/,$p' | sed 's/^ok>//' > got.txt
python3 "$ROOT/scripts/getblocks.py" lfblocks.fb4 >> got.txt

# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "editor tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "editor tests FAILED (output saved as got.txt)"
    exit 1
fi
