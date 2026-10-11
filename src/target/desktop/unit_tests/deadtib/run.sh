#!/bin/sh
# stop-tib test: a running app with `stop-tib` set gets the keyboard.
# After core.f, an app is compiled that echoes every key and, on `q`, loops
# without `break` until the VM times out and stops it. `cold` and `1 stop-tib !`
# start it with more lines still waiting on stdin: the app must echo them
# (rather than lf spinning with a key waiting that nobody reads), and once
# it stops, the terminal must read the rest. Runs in a temp dir and compares
# the output with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
cp "$ROOT/bin/lfblocks.fb4" .

# A hang is the failure this guards against: give up after a while
TIMEOUT=""
if command -v timeout > /dev/null 2>&1; then TIMEOUT="timeout 60"; fi

{
cat "$ROOT/forth/core.f"
cat <<'F'

3 >options
0 open-flash
:noname
    begin
        key? if
            key dup emit  113 = if  begin again  then
        then
        break
    again
; hex 80000000 ,jump decimal
close-flash
.( app-ready ) cr
cold 1 stop-tib !
abc 1 2 + .q
.( terminal-back ) cr
bye
F
} | $TIMEOUT "$LF" -o 3 > out.txt 2>&1 || true

sed -n '/app-ready/,$p' out.txt | sed 's/^\(ok>\)*//' > got.txt
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "deadtib tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "deadtib tests FAILED (output saved as got.txt)"
    exit 1
fi
