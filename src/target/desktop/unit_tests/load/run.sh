#!/bin/sh
# Regression test for LOAD and -->: runs bin/lf on a scratch block file and
# compares its output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

# Let lf create blank block and flash files, then write test screens.
echo bye | "$LF" -o 3 > /dev/null
blk() {  # blk n "text": write a screen, padded with spaces to 4096 bytes
    printf '%-4096s' "$2" | dd of=lfblocks.bin bs=4096 seek="$1" conv=notrunc 2>/dev/null
}
blk 1 '.( [1] )'
blk 2 '.( <2 ) 3 load .( 2> )'           # nesting deeper than the
blk 3 '.( <3 ) 4 load .( 3> )'           # 2 block buffers, so the
blk 4 '.( [4] )'                         # callers' buffers are evicted
n=10; while [ $n -lt 21 ]; do blk $n ".( $n ) -->"; n=$((n+1)); done
blk 21 '.( 21 )'                         # 12 chained screens
blk 30 '.( <30 ) 31 load .( never )'
blk 31 '.( [31] ) nosuchword .( never )' # error inside a nested load
n=40; while [ $n -lt 49 ]; do blk $n ".( $n ) $((n+1)) load"; n=$((n+1)); done
blk 50 '.( [50] ) bye .( never )'

"$LF" -o 3 > out.txt 2>&1 <<'F' || true
0 open-flash
1 load .( after-same-line )
: foo 1 load 42 . ;
foo 7 .
2 load
10 load .( chain-done )
30 load
.( recovered )
40 load
.( recovered-again )
50 load .( never )
F
tail -n +2 out.txt > got.txt   # drop the version banner
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "load tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "load tests FAILED (output saved as got.txt)"
    exit 1
fi
