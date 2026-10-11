#!/bin/sh
# Test of head-, head+ and -heads (temporary headers): temporary words in
# two wordlists, permanent words defined after head+ that use them, a second
# head- ... head+ round, the -122 errors from save-wids and close-flash
# before -heads, -heads dropping the names but not the code (and relinking
# the permanent headers), and -8 when the header buffer is full. Runs bin/lf in a temp dir
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
head- head-
: tmp1  10 ;
: tmp2  tmp1 1 + ;
1 current !  : tmproot  20 ;  0 current !
tmp1 tmp2 + .( sum ) . cr
only tmproot forth .( root ) . cr
head+ head+
: perm2  tmp2 perm1 + ;
perm2 .( perm2 ) . cr
tmp1 .( tmp1 still here ) . cr
head-
: tmp3  tmp2 2* ;
head+
: perm3  tmp3 perm2 + ;
save-wids
close-flash
-heads -heads
perm2 .( perm2 ) . cr
perm3 .( perm3 ) . cr
perm1 .( perm1 ) . cr
tmp1
tmp2
tmp3
only tmproot
forth
head-
F
# more temporary headers than the buffer holds: -8 (dictionary overflow)
n=0; while [ $n -lt 400 ]; do echo ": many$n ;"; n=$((n+1)); done
cat <<'F'
head+ -heads
many1
perm3 .( still ) . cr
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
