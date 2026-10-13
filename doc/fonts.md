# Bitmap fonts

`scripts/mkfont.py` makes 4-bit anti-aliased bitmap fonts for the graphic
LCDs from TrueType/OpenType fonts such as Noto Sans and Noto Sans CJK.
Each pixel is 0 (background) to 15 (foreground); at run time a 16-entry
color table, interpolated between the background and foreground colors,
turns it into what is sent to the LCD.

## Making a font

```
py scripts\mkfont.py -f NotoSansCJKsc-Regular.otf -s 24 -o cjk24.lfnt --proof cjk24.png glyphs.txt
```

| Option | Meaning |
|---|---|
| `-f FILE` | The font: `.ttf`, `.otf`, or `.ttc` (with `--face N` after it for the face in the collection). A bare file name that isn't in the current folder is looked for in the installed font folders. Give `-f` more than once for fallback fonts: each glyph comes from the first font that has it, and the first font sets the cell height and ascent (grown if a fallback glyph needs more room) |
| `-s PIXELS` | The size: the font's em square, in pixels |
| `-w WEIGHT` | The weight: 100 to 900, or a name (`thin` 100, `extralight` 200, `light` 300, `demilight` 350, `regular` 400, `medium` 500, `semibold` 600, `bold` 700, `extrabold` 800, `black` 900). A variable font (one with a weight axis, like Noto Sans or `NotoSansCJKsc-VF.otf`) is set to it. A static font must already be that weight: Noto Sans CJK comes as one file per weight (`NotoSansCJK-Bold.ttc`, ...) |
| `-o FILE` | The output (default `font.lfnt`) |
| `--max-bytes N` | The most the file may take. Glyphs are added in the list's order while they fit; the rest are listed as dropped |
| `--blocks` | Lays the font out for reading from the block file: no glyph record crosses a 4 KB block, so one block read gets a whole glyph. See [In the block file](#in-the-block-file) |
| `--proof FILE` | A PNG of every glyph, decoded from the output file, so it shows what the LCD will get |
| `--list [TEXT \| PATH ...]` | Lists the fonts installed on this computer (on Windows `C:\Windows\Fonts` and your own `%LOCALAPPDATA%\Microsoft\Windows\Fonts`; on Linux and macOS the usual font folders): family, style, weight (or a variable font's weight range), `--face N` for each face of a `.ttc`, and the file. Words keep only the fonts whose file, family or style contains them all; a file or folder lists just that |
| `--selftest` | Tests the encoder and decoder on made-up bitmaps |

To find a font and the options it needs:

```
py scripts\mkfont.py --list noto cjk
Noto Sans CJK SC             Bold         700              --face 2  C:\...\NotoSansCJK-Bold.ttc
Noto Sans CJK SC             Regular      400              --face 2  C:\...\NotoSansCJK-Regular.ttc
```

It needs Pillow and fontTools (`pip install pillow fonttools`). Install
them for the Python that runs the script: on Windows, `py -m pip install
pillow fonttools`, since a plain `pip` may belong to a different Python
than `py`. If they're missing, `mkfont.py` prints the command to use.

`glyphs.txt` is UTF-8. A line like `U+0020-U+007E` or `U+00B0` adds those
code points; any other line adds each of its characters, so a line of
Chinese text adds those characters. The order is the priority. Duplicates,
code points above U+FFFF and characters the font lacks are skipped and
reported. For example:

```
U+0020-U+007E
学如不及，犹恐失之
```

For scripts the first font lacks, add fonts that have them:

```
py scripts\mkfont.py -f NotoSansCJKsc-Regular.otf -f NotoSansArabic-Regular.ttf -f NotoSansDevanagari-Regular.ttf -s 24 -o all24.lfnt glyphs.txt
```

Glyphs are drawn one code point at a time, with no text shaping. That
suits Latin, Greek, Cyrillic, Hebrew and CJK. Arabic letters change shape
with their neighbors: put the presentation forms (U+FB50-U+FDFF,
U+FE70-U+FEFF) in the glyph list and pick the form for each letter, and
the right-to-left order, when drawing. Indic scripts such as Devanagari
need a shaping engine (conjuncts, reordered vowel signs, ligatures that
have no code point of their own), so per-code-point glyphs only give
isolated letters.

## The file

All numbers are little-endian, so a font loaded into VM memory reads the
same way through slices: a byte at byte offset `o` is the 8-bit slice at
cell `o/4`, shift `8*(o mod 4)`, and a 16-bit value the 16-bit slice there.

| Offset | Size | Contents |
|---|---|---|
| 0 | 4 | `LFNT` |
| 4 | 1 | Version (2; version 1 had white and black runs as format 00) |
| 5 | 1 | Bits per pixel (4) |
| 6 | 1 | Cell height: the line height, in pixels |
| 7 | 1 | Ascent: the baseline's row in the cell |
| 8 | 2 | Glyph count n |
| 10 | 2 | Weight (100 to 900) |
| 12 | 257 x 2 | Page table: `page[p]` is the index of the first glyph whose code point is at least `p * 256`; `page[256]` = n |
| 526 | 2 | 0 (so the index is cell-aligned) |
| 528 | n x 4 | Index, sorted by code point: bits 31:24 are the code point's low byte, bits 23:0 the glyph record's byte offset in the file |
| ... | | Glyph records, each 2-byte aligned |

To find code point `cp`: the glyphs of page `p = cp >> 8` are index entries
`page[p]` to `page[p+1] - 1`; binary search them for the low byte
`cp & 255`. A page with no glyphs costs nothing but its two table bytes.

A glyph record:

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | Width w of the bounding box |
| 1 | 1 | Height h |
| 2 | 1 | Horizontal offset of the box in the cell |
| 3 | 1 | Vertical offset of the box from the top of the cell |
| 4 | 1 | Advance: the cell's width, where the next glyph starts |
| 5 | 1 | 0 |
| 6 | 2 each | 16-bit codes, until w x h pixels have been described |

Drawing a glyph: set the LCD's window to the bounding box (cell origin plus
the offsets, w by h), clear the rest of the cell to the background if
needed, and stream the pixels row by row as the codes describe them. A
space has w = h = 0 and no codes.

## In the block file

A font in the block file starts at byte 32 of a block (`BLOCK_OFFSET` in
`mkfont.py`); the first 32 bytes are kept for an HMAC of the font. The
font then runs on through the following blocks as one stream, so file
offset `o` is at byte `(o + 32) mod 4096` of block `first + (o + 32) / 4096`.

With `--blocks`, mkfont places the glyph records so that none crosses a
block. It goes through the glyphs in priority order and puts each one in
the first gap that holds it, which keeps the most-used glyphs together in
the first blocks; the gaps are zeros. The header, page table and index
aren't moved: their 2- and 4-byte entries are aligned, so no single entry
crosses a block, though the index as a whole may span several.

The padding is small. For 595 glyphs (ASCII and 500 common hanzi) from
Noto Sans CJK it is 390 bytes at 16 pixels, 850 at 24, 1152 at 32 and
5790 at 48 (2.2% of 260 KB).

## Pixel codes

Each 16-bit code describes a few pixels. "White" is 15 (foreground) and
"black" is 0 (background):

| Bits 15:14 | Fields | Pixels |
|---|---|---|
| 00 | gray[4] black[3] gray[4] white[3] | a pixel, `black` (0 to 7) black pixels, a pixel, then `white` (0 to 7) white pixels |
| 01 | gray[4] next[1] length[9] | the pixel `gray`, then `length` (0 to 511) pixels of `next` (1 = white, 0 = black) |
| 10 | gray[4] gray[4] next[1] length[5] | two pixels, then `length` (0 to 31) pixels of `next` |
| 11 | gray[4] gray[4] gray[4] black[2] | three pixels, then 0 to 3 black pixels |

A "gray" can be any value 0 to 15. The encoder finds the shortest sequence
of codes for each glyph (dynamic programming over the four formats).

The formats were chosen by measuring Noto Sans CJK (Regular and Bold, 12
to 32 pixels, Latin and CJK):

- Format 11 as three grays and a short black run beat three grays with a
  run of either color, or of white.
- Version 1's format 00, a white run then a black run (0 to 127 each), was
  the least used: 2.5% of the codes. Two grays with short runs in its
  place (an edge, a gap, an edge, a stroke) cut the codes by 12.4%; the
  other order, gray white gray black, by 11.8%.

The pixel data comes out about 1.1x (12 pixels) to 1.8x (32 pixels)
smaller than plain 4-bit pixels in the same bounding boxes; small
anti-aliased glyphs are mostly edges.
