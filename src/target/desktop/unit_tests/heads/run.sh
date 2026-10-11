#!/bin/sh
# Test of head- and head+ (temporary headers): temporary words in two
# wordlists, a permanent `label` resolved by a temporary-time `:`, the -122
# errors from save-wids and close-flash, head+ dropping the names but not
# the code, and -8 when the header buffer is full. Runs bin/lf in a temp dir
# on copies of the bin/ images and compares the output, minus the banner
# line, with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" .

{
cat <<'F'
0 open-flash
: perm1  1 ;
label later
head- head-
: tmp1  10 ;
: tmp2  tmp1 1 + ;
1 current !  : tmproot  20 ;  0 current !
: later  tmp2 perm1 + ;
tmp1 tmp2 + .( sum ) . cr
only tmproot forth .( root ) . cr
save-wids
close-flash
head+ head+
later .( later ) . cr
tmp1
tmp2
only tmproot
forth
perm1 .( perm1 ) . cr
head-
F
# more temporary headers than the buffer holds: -8 (dictionary overflow)
n=0; while [ $n -lt 400 ]; do echo ": many$n ;"; n=$((n+1)); done
cat <<'F'
head+
many1
later .( still ) . cr
close-flash
bye
F
} | "$LF" -o 3 > out.txt 2>&1 || true

# the overflow happens at a size that depends on the pointer width
tail -n +2 out.txt | awk '/ior=-8 / { if (seen++) next } { print }' > got.txt
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "heads tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "heads tests FAILED (output saved as got.txt)"
    exit 1
fi
