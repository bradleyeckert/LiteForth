#!/bin/sh
# Regression test for booting from flash (-o 8): one run of bin/lf compiles
# a small dictionary and app, ends with `save-wids` and close-flash; a second run
# boots from that flash image, which the desktop loads at a different address,
# so the saved wordlists must not depend on C addresses. Also checks that a
# flash image without a valid record is reported and not run. Runs in a temp
# dir and compares the output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfblocks.bin" .

# 1. No boot record: lf creates a blank lfflash.bin.
printf '.( no-record ) cr\nbye\n' | "$LF" -o 11 > out.txt 2>&1 || true

# 2. Build a flash image with a boot record. The app and yeet handler just
#    `break` in a loop, so they print nothing. (Definitions from go.f.)
"$LF" -o 3 > /dev/null 2>&1 <<'F'
0 open-flash
: cells ; immediate
hex
: _section  ( n -- )        dp^ ! ;
: _udata    ( -- )          0 _section ;
: _code     ( -- )          2 _section ;
: _text     ( -- )          3 _section ;
: 'here     ( -- a )        dp^ @ 2* dp[] + ;
: here      ( -- a )        'here @ ;
: chere     ( -- addr )     |inst [ dp[] 4 cells + ] literal @ ;
: rshift    ( u1 u2 -- u3 ) shft[ ]shr ;
: iaddr     ( a1 -- a2 )    dup 2* swap 1A rshift 1 and + ;
: _again    ( a inst -- )   >r iaddr  chere iaddr inv + 1FF and r> + ,inst ;
: begin     ( -- a )        chere ; immediate
: again     ( a -- )        _bran _again ; immediate
: ,jump  ( xt addr -- )
   _code here >r  'here ! ,compile  postpone exit
   r> 'here ! _udata
;
:noname begin break again ; 80000000 ,jump
:noname begin break again ; 80000002 ,jump
decimal
: boot-test  42 . ;
create extra wordlist drop
1 current !  : only-mark  7 . ;  0 current !
_text here 32 bit  1 !  save-wids
close-flash
bye
F

# 3. Boot from it: the wordlists, a flash word, the `only` list down to its
#    built-in words, and an error in a word doesn't stop the terminal.
"$LF" -o 11 >> out.txt 2>&1 <<'F'
0 .wid 1 .wid 2 .wid cr
boot-test cr
only only-mark forth boot-test cr
nosuchword
boot-test cr
bye
F

tail -n +2 out.txt | grep -v '^\[' > got.txt   # drop the version banners
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "boot tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "boot tests FAILED (output saved as got.txt)"
    exit 1
fi
