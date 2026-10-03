#!/bin/sh
# Regression test for `label`: running a label at the terminal before a `:`
# resolves it is an error (code that calls it before then jumps to cell 0,
# so this doesn't), and `bye` with a label still unresolved reports it and makes lf
# exit with -119 (137). Runs in a temp dir and compares the output with
# expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.bin" .

status=0
"$LF" -o 3 > out.txt 2>&1 <<'F' || status=$?
0 open-flash
label fwd
: use-fwd  fwd 1 + ;
fwd
: fwd  41 ;
use-fwd .
label never
: calls-never  never ;
close-flash
bye
F
echo "exit $status" >> out.txt

tail -n +2 out.txt > got.txt   # drop the version banner
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "label tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "label tests FAILED (output saved as got.txt)"
    exit 1
fi
