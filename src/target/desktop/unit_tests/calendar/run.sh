#!/bin/sh
# Test of the calendar words (forth/core.f, Calendar block): loads core.f
# into bin/lf with copies of bin/lfflash.bin and bin/lfblocks.fb4, prints
# the date and time of about 400 times (edge cases, the ends of months in
# leap and common years, 2^32 - 1 and random ones) with .date and .time,
# converts each back with date> and time>, and checks leap? on a few years.
# check.py compares the output with Python's datetime. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
if ! command -v python3 > /dev/null 2>&1; then
    echo "calendar tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" .
python3 "$HERE/check.py" make > test.f
TIMEOUT=""                              # a conversion that never ends fails the test
command -v timeout > /dev/null 2>&1 && TIMEOUT="timeout 60"
{ cat "$ROOT/forth/core.f"; cat test.f; } | $TIMEOUT "$LF" -o 3 > out.txt 2>&1 \
    || { echo "calendar test FAILED: lf stopped or timed out"; exit 1; }
python3 "$HERE/check.py" compare out.txt || { echo "calendar test FAILED"; exit 1; }
echo "calendar tests passed"
