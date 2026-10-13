#!/bin/sh
# Tests scripts/mkfont.py: its encoder/decoder self test (pure Python), then,
# if Pillow, fontTools and a Noto Sans CJK font are present, a real build
# that the script checks by looking up and decoding every glyph.
HERE=$(cd "$(dirname "$0")" && pwd)
MKFONT=$HERE/../../../../../scripts/mkfont.py
if ! command -v python3 > /dev/null 2>&1; then
    echo "mkfont tests skipped (no python3)"
    exit 0
fi
python3 "$MKFONT" --selftest || exit 1
FONT=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
if [ ! -f "$FONT" ] || ! python3 -c "import PIL, fontTools" 2> /dev/null; then
    echo "mkfont font build skipped (needs Pillow, fontTools and $FONT)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
printf 'U+0020-U+007E\n学如不及，犹恐失之\n' > "$WORK/glyphs.txt"
python3 "$MKFONT" -f "$FONT" -s 16 -o "$WORK/f.lfnt" --proof "$WORK/f.png" "$WORK/glyphs.txt" > "$WORK/out.txt" || { cat "$WORK/out.txt"; exit 1; }
grep -q "104 glyphs" "$WORK/out.txt" || { echo "mkfont test FAILED:"; cat "$WORK/out.txt"; exit 1; }
python3 "$MKFONT" -f "$FONT" -s 16 -o "$WORK/g.lfnt" --max-bytes 3000 "$WORK/glyphs.txt" > "$WORK/out2.txt" || exit 1
size=$(wc -c < "$WORK/g.lfnt")
if [ "$size" -gt 3000 ] || ! grep -q "dropped" "$WORK/out2.txt"; then
    echo "mkfont test FAILED: --max-bytes"; cat "$WORK/out2.txt"; exit 1
fi
# -w: a static font must already be that weight
BOLD=/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc
if [ -f "$BOLD" ]; then
    python3 "$MKFONT" -f "$BOLD" -s 16 -w bold -o "$WORK/b.lfnt" "$WORK/glyphs.txt" > "$WORK/out3.txt" || exit 1
    grep -q "weight 700" "$WORK/out3.txt" || { echo "mkfont test FAILED: -w bold"; cat "$WORK/out3.txt"; exit 1; }
fi
if python3 "$MKFONT" -f "$FONT" -s 16 -w bold -o "$WORK/x.lfnt" "$WORK/glyphs.txt" > /dev/null 2>&1; then
    echo "mkfont test FAILED: -w bold on a regular-only font should fail"; exit 1
fi
# several -f: Hebrew isn't in Noto Sans CJK, so it comes from the fallback
DEJAVU=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
if [ -f "$DEJAVU" ]; then
    printf 'AB\nאב\n学\n' > "$WORK/fb.txt"
    python3 "$MKFONT" -f "$FONT" -f "$DEJAVU" -s 16 -o "$WORK/fb.lfnt" "$WORK/fb.txt" > "$WORK/out4.txt" || { cat "$WORK/out4.txt"; exit 1; }
    grep -q "5 glyphs" "$WORK/out4.txt" && grep -q "2 glyph(s) from .*DejaVuSans" "$WORK/out4.txt" \
        || { echo "mkfont test FAILED: fallback font"; cat "$WORK/out4.txt"; exit 1; }
fi
# --list: the faces of a .ttc, filtered by words
python3 "$MKFONT" --list "$FONT" sc > "$WORK/list.txt" || exit 1
grep -q "Noto Sans CJK SC .*Regular .*400 .*--face 2 " "$WORK/list.txt" || { echo "mkfont test FAILED: --list"; cat "$WORK/list.txt"; exit 1; }
echo "mkfont tests passed"
