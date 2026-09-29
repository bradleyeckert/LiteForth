#!/bin/sh
# Regression test for QUIT: error reporting and recovery, stack depth
# checks, TIB overflow and bye. Runs bin/lf in a temp dir and compares its
# output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

long=$(printf '%0200d' 0 | tr 0 ' ')    # longer than TIB
seq32='1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32'
{
    echo '.( [undefined] ) nosuchword .( never )'
    echo '.( [after error] ) 1 . base @ .'
    echo 'hex 1 0 drop drop drop'       # underflow; base goes back to decimal
    echo '.( [base] ) 10 .'
    echo "$seq32"                       # 32 items: fine
    echo ".s $seq32"                    # 64 items: overflow
    echo "1 . .( [long] ) $long 2 ."    # too long: nothing runs
    echo '3 .'
    echo '1 2 3 .s drop drop drop .s'
    echo 'drop bye .( never )'          # bye wins over the stack check
} > in.txt
"$LF" -o 3 < in.txt > out.txt 2>&1 || true
tail -n +2 out.txt > got.txt            # drop the version banner
if diff -u "$HERE/expected.txt" got.txt; then
    echo "quit tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "quit tests FAILED (output saved as got.txt)"
    exit 1
fi
