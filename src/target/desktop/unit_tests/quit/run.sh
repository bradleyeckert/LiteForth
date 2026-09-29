#!/bin/sh
# Regression test for QUIT: error reporting and recovery, stack depth
# checks (111 items allowed, 112 overflow, 8 extra drops underflow), TIB
# overflow and bye. Runs bin/lf in a temp dir and compares its
# output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

long=$(printf '%0200d' 0 | tr 0 ' ')    # longer than TIB
ones56=$(printf '1 %.0s' $(seq 56))       # 56 items, fits in TIB
ones55=$(printf '1 %.0s' $(seq 55))
drops8=$(printf 'drop %.0s' $(seq 8))
{
    echo '.( [undefined] ) nosuchword .( never )'
    echo '.( [after error] ) 1 . base @ .'
    echo 'hex 1 0 drop drop drop'       # underflow; base goes back to decimal
    echo '.( [base] ) 10 .'
    echo "$ones56"                      # 56 items: fine
    echo "$ones55"                      # 111 items: still fine
    echo '.s 1'                         # 112 items: overflow
    echo "$drops8"                      # 8 too many drops: underflow
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
