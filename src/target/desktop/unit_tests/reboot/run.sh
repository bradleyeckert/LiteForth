#!/bin/sh
# reboot test: on the desktop `reboot` restarts lf's interpreter (lfReset),
# as a reset does on the board: the banner again, and the
# lines after it still run. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" "$WORK"
GOT=$(cd "$WORK" && printf '7 .\nreboot\n2 3 + .\nbye\n' | "$LF" -o 3 2>&1)
BANNERS=$(printf '%s\n' "$GOT" | grep -c 'v0\.' || true)
if [ "$BANNERS" = 2 ] && printf '%s' "$GOT" | grep -q '^7 ' && printf '%s' "$GOT" | grep -q '5 *$'; then
    echo "reboot tests passed"
else
    printf 'reboot tests FAILED; lf printed:\n%s\n' "$GOT"
    exit 1
fi
