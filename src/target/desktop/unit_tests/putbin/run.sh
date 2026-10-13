#!/bin/sh
# Tests scripts/putbin.py: writes binary files into a fresh signed block file
# and checks the layout (32 zero bytes, the data, zeros to the block's end),
# the untouched neighbors, extending the file, -n, the errors, and that
# getblocks.py shows binary blocks as `( BLOCK IGNORE )` lines that
# putblocks.py skips when the dump is written back.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
if ! command -v python3 > /dev/null 2>&1; then
    echo "putbin tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
fail() { echo "putbin test FAILED: $1"; [ -f out.txt ] && cat out.txt; exit 1; }
{ printf 'LITEFORTH'; head -c $((16 * 4096 - 9)) /dev/zero | tr '\000' ' '; } > b.fb4
printf '( BLOCK 3 )\n\\ some text\n' > t.f
python3 "$ROOT/scripts/putblocks.py" t.f b.fb4 > /dev/null || fail "putblocks"
python3 -c "import random; r=random.Random(5); open('a.bin','wb').write(bytes(r.randrange(256) for _ in range(9000)))"
printf 'small' > s.bin
cp b.fb4 before.fb4
python3 "$ROOT/scripts/putbin.py" -n a.bin 10 b.fb4 > out.txt || fail "-n"
cmp -s b.fb4 before.fb4 || fail "-n changed the file"
python3 "$ROOT/scripts/putbin.py" a.bin 10 b.fb4 > out.txt || fail "a.bin"
grep -q "blocks 10-12, next free block 13" out.txt || fail "block range"
python3 "$ROOT/scripts/putbin.py" s.bin 20 b.fb4 > out.txt || fail "s.bin past the end"
grep -q "block 20, next free block 21" out.txt && grep -q "extended from 16 to 21 blocks" out.txt || fail "extend"
python3 - "$ROOT" <<'PY' || fail "layout"
import sys
d = open("b.fb4", "rb").read(); B = 4096
a = open("a.bin", "rb").read()
img = d[10 * B:13 * B]
assert img[:32] == bytes(32), "HMAC bytes"
assert img[32:32 + len(a)] == a, "data"
assert img[32 + len(a):] == bytes(3 * B - 32 - len(a)), "tail"
assert d[9 * B:10 * B] == b" " * B and d[13 * B:14 * B] == b" " * B, "neighbors"
assert d[20 * B:21 * B] == bytes(32) + b"small" + bytes(B - 37), "small file"
assert d[16 * B:20 * B] == b" " * (4 * B), "extension blanks"
assert len(d) == 21 * B
PY
for bad in "a.bin 0 b.fb4" "a.bin x b.fb4" "missing.bin 5 b.fb4" "a.bin 5 t.f"; do
    cp b.fb4 before.fb4
    if python3 "$ROOT/scripts/putbin.py" $bad > out.txt 2>&1; then fail "accepted: $bad"; fi
    cmp -s b.fb4 before.fb4 || fail "changed the file: $bad"
done
python3 "$ROOT/scripts/getblocks.py" b.fb4 > dump.f 2> err.txt || fail "getblocks"
[ "$(grep -c '^( BLOCK IGNORE ) block 1[012]: binary, crc32 ' dump.f)" = 3 ] || { cat dump.f; fail "getblocks binary lines"; }
grep -q '^( BLOCK IGNORE ) block 20: binary' dump.f && grep -q '^\\ some text' dump.f || { cat dump.f; fail "getblocks"; }
[ -s err.txt ] && { cat err.txt; fail "getblocks warned"; }
cp b.fb4 before.fb4
python3 "$ROOT/scripts/putblocks.py" dump.f b.fb4 > /dev/null 2>&1 || fail "putblocks dump"
cmp -s b.fb4 before.fb4 || fail "the dump written back changed the file"
echo "putbin tests passed"
