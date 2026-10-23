#!/bin/sh
# With the flash pages locked (lf -L 2, as at SECURITY_LEVEL 2, where only
# update packages write them), every word that writes them or copies them
# out refuses with -20, and the flash file is unchanged. Without -L the
# same words work. Usage: run.sh LF
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" "$T"
cd "$T" || exit 1
before=$(cksum < lfflash.bin)
fail=0
for words in '0 open-flash' 'close-flash' 'save-pages' ': foo ;' 'save-wids'; do
    got=$(printf '%s\nbye\n' "$words" | "$LF" -o 3 -L 2 2>&1)
    case "$got" in
    *"ior=-20"*) ;;
    *) echo "locked: \"$words\" wasn't refused with -20:"; echo "$got"; fail=1 ;;
    esac
done
[ "$(cksum < lfflash.bin)" = "$before" ] || { echo "locked: the flash file changed"; fail=1; }
got=$(printf '0 open-flash\nclose-flash\n1 2 + .\nbye\n' | "$LF" -o 3 2>&1)
case "$got" in
*"ior="*) echo "locked: without -L, open-flash and close-flash failed:"; echo "$got"; fail=1 ;;
esac
[ $fail = 0 ] || { echo "locked tests FAILED"; exit 1; }
echo "locked tests passed: open-flash, close-flash, save-pages, : and save-wids refused at level 2"
