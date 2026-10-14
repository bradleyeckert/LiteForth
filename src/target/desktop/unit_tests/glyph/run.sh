#!/bin/sh
# Test of `glyph` and `font-cache` (forth/core.f, Font lookup block): loads
# core.f into bin/lf with copies of bin/lfflash.bin and bin/lfblocks.fb4,
# which holds fonts at blocks 100 and 120 (fonts/build.bat), looks up every
# glyph of both fonts and some code points they lack, and compares the first
# four 16-bit slices of each record with what check.py reads from the block
# file. It does that three times: with no font cached, with font 100 in
# flash page 3, and with font 120 there. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
if ! command -v python3 > /dev/null 2>&1; then
    echo "glyph tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" .
python3 "$HERE/check.py" make lfblocks.fb4 100 120 > test.f || { echo "glyph tests skipped (no fonts at blocks 100 and 120)"; exit 0; }
TIMEOUT=""                              # a lookup that never ends fails the test
command -v timeout > /dev/null 2>&1 && TIMEOUT="timeout 60"
{ cat "$ROOT/forth/core.f"; cat test.f; } | $TIMEOUT "$LF" -o 3 > out.txt 2>&1 \
    || { echo "glyph test FAILED: lf stopped or timed out"; exit 1; }
python3 "$HERE/check.py" compare out.txt lfflash.bin || { echo "glyph test FAILED"; exit 1; }
echo "glyph tests passed"
