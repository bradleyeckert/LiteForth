#!/bin/sh
# save-pages test: copies LiteForth's flash pages into lfpages.bin, as an
# app package needs them (doc/admin/firmware-updates.md section 5), and
# refuses with no lfpages.bin or with a page open. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/bin/lfflash.bin" "$ROOT/bin/lfblocks.fb4" "$WORK"
cd "$WORK"
fail() {
    printf 'save-pages tests FAILED: %s\nlf printed:\n%s\n' "$1" "$2"
    exit 1
}

# No lfpages.bin: -92
GOT=$(printf 'save-pages\nbye\n' | "$LF" -o 3 2>&1)
printf '%s' "$GOT" | grep -q 'ior=-92' || fail "no lfpages.bin: not -92" "$GOT"

# Too small: -92
dd if=/dev/zero of=lfpages.bin bs=4096 count=63 2>/dev/null
GOT=$(printf 'save-pages\nbye\n' | "$LF" -o 3 2>&1)
printf '%s' "$GOT" | grep -q 'ior=-92' || fail "short lfpages.bin: not -92" "$GOT"

# A page open: -20, and nothing written
dd if=/dev/zero of=lfpages.bin bs=4096 count=64 2>/dev/null
GOT=$(printf '0 open-flash\nsave-pages\nclose-flash\nbye\n' | "$LF" -o 3 2>&1)
printf '%s' "$GOT" | grep -q 'ior=-20' || fail "a page open: not -20" "$GOT"
[ "$(tr -d '\0' < lfpages.bin | wc -c)" = 0 ] || fail "a page open: lfpages.bin was written" "$GOT"

# The pages, byte for byte: the flash file's 64 KB, then what the rest of
# the four pages hold (lf's flash beyond its file)
GOT=$(printf 'save-pages\nbye\n' | "$LF" -o 3 2>&1)
printf '%s' "$GOT" | grep -q 'ior=' && fail "save-pages gave an error" "$GOT"
[ "$(wc -c < lfpages.bin)" = 262144 ] || fail "lfpages.bin changed size" "$GOT"
cmp -s -n 65536 lfpages.bin lfflash.bin || fail "page 0 differs from lfflash.bin" "$GOT"
echo "save-pages tests passed"
