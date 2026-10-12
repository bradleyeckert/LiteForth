#!/bin/sh
# xmodem test: sender.py plays an XMODEM (checksum) sender against bin/lf in
# a temp dir with a fresh 64-block file, including a corrupt packet, a
# repeated one, 1K packets, source errors and a cancel; then getblocks.py
# dumps the blocks. Compares it all with expected.txt. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
if ! command -v python3 > /dev/null 2>&1; then
    echo "xmodem tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
{ printf 'LITEFORTH'; head -c $((64 * 4096 - 9)) /dev/zero | tr '\000' ' '; } > "$WORK/lfblocks.fb4"

{
python3 "$HERE/sender.py" "$LF" "$WORK"
echo "-- the blocks"
python3 "$ROOT/scripts/getblocks.py" "$WORK/lfblocks.fb4"
} > "$WORK/got.txt" 2>&1 || true

# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" "$WORK/got.txt"; then
    echo "xmodem tests passed"
else
    cp "$WORK/got.txt" "$HERE/got.txt"
    echo "xmodem tests FAILED (output saved as got.txt)"
    exit 1
fi
