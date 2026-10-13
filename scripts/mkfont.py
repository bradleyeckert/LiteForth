#!/usr/bin/env python3
"""Make a 4-bit anti-aliased bitmap font for LiteForth's graphic LCDs.

Usage: mkfont.py -f font.otf -s size [options] glyphs.txt

Renders the glyphs listed in glyphs.txt from a TrueType/OpenType font
(Noto Sans, Noto Sans CJK, ...) at `size` pixels, compresses each one and
writes a font file (default font.lfnt) with an index for looking glyphs
up by their 16-bit code point. See doc/fonts.md for the format.

glyphs.txt is UTF-8. A line like `U+0020-U+007E` or `U+00B0` adds code
points; any other line adds each of its characters (so a line of Chinese
text adds those characters). The order is the priority: with
--max-bytes, glyphs are added in order while they fit, and the ones that
don't are listed. Duplicates, code points above U+FFFF and characters the
font doesn't have are skipped and reported.

Options:
  -f FILE        the font (.ttf, .otf or .ttc)
  --face N       the face in a .ttc collection (default 0)
  -s PIXELS      the size (the font's em square, in pixels)
  -w WEIGHT      the weight: 100 to 900, or thin, extralight, light,
                 demilight (350), regular, medium, semibold, bold,
                 extrabold, black. A variable font is set to it; a static
                 font must already be that weight (Noto Sans CJK has one
                 file per weight: NotoSansCJK-Bold.ttc, ...)
  -o FILE        the output (default font.lfnt)
  --max-bytes N  the most the font file may take
  --proof FILE   also draw the glyphs, decoded from the file, as a PNG
  --selftest     test the encoder and decoder on made-up bitmaps

Needs Pillow (pip install pillow) and fontTools (pip install fonttools).

Pixels are 0 (background, "black") to 15 (foreground, "white"), streamed
row by row through the glyph's bounding box. Each 16-bit code is one of:
  00 gray[4] black[3] gray[4] white[3]   a pixel, 0-7 black, a pixel, 0-7 white
  01 gray[4] next[1] len[9]      a pixel, then len pixels of next (1 white)
  10 gray[4] gray[4] next[1] len[5]
  11 gray[4] gray[4] gray[4] black[2]
The encoder picks the shortest sequence of codes for each glyph.
"""
import re
import struct
import sys

MAGIC = b"LFNT"
VERSION = 2                     # 2: format 00 is gray black gray white
WHITE, BLACK = 15, 0

# ---- the pixel codec ---------------------------------------------------------


def _run(px, i, color, cap):
    n = 0
    while i + n < len(px) and px[i + n] == color and n < cap:
        n += 1
    return n


def encode(px):
    """Returns the 16-bit codes for a list of 4-bit pixels (shortest)."""
    n = len(px)
    best = [0] * (n + 1)            # codes needed from position i
    choice = [None] * (n + 1)       # (code, next position)
    for i in range(n - 1, -1, -1):
        options = []
        b = _run(px, i + 1, BLACK, 7)                            # 00
        k = i + 1 + b                   # the second pixel
        if k < n:
            w = _run(px, k + 1, WHITE, 7)
            options.append(((px[i] << 10) | (b << 7) | (px[k] << 3) | w, k + 1 + w))
        j = i + 1                                                # 01
        if j < n and px[j] in (WHITE, BLACK):
            nxt = px[j] == WHITE
            L = _run(px, j, px[j], 511)
        else:
            nxt, L = 0, 0
        options.append((0x4000 | (px[i] << 10) | (nxt << 9) | L, j + L))
        if i + 1 < n:                                            # 10
            j = i + 2
            if j < n and px[j] in (WHITE, BLACK):
                nxt = px[j] == WHITE
                L = _run(px, j, px[j], 31)
            else:
                nxt, L = 0, 0
            options.append((0x8000 | (px[i] << 10) | (px[i + 1] << 6) | (nxt << 5) | L,
                            j + L))
        if i + 2 < n:                                            # 11
            j = i + 3
            L = _run(px, j, BLACK, 3)
            options.append((0xC000 | (px[i] << 10) | (px[i + 1] << 6) | (px[i + 2] << 2) | L,
                            j + L))
        code, k = min(options, key=lambda o: best[o[1]])
        best[i] = best[k] + 1
        choice[i] = (code, k)
    codes = []
    i = 0
    while i < n:
        code, i = choice[i]
        codes.append(code)
    return codes


def decode(codes, count):
    """Returns the first `count` pixels the codes describe."""
    px = []
    for c in codes:
        f = c >> 14
        if f == 0:
            px += ([(c >> 10) & 15] + [BLACK] * ((c >> 7) & 7)
                   + [(c >> 3) & 15] + [WHITE] * (c & 7))
        elif f == 1:
            px += [(c >> 10) & 15] + [WHITE if (c >> 9) & 1 else BLACK] * (c & 511)
        elif f == 2:
            px += [(c >> 10) & 15, (c >> 6) & 15] + [WHITE if (c >> 5) & 1 else BLACK] * (c & 31)
        else:
            px += [(c >> 10) & 15, (c >> 6) & 15, (c >> 2) & 15] + [BLACK] * (c & 3)
    return px[:count]


# ---- glyph lists -------------------------------------------------------------

WEIGHTS = {"thin": 100, "extralight": 200, "light": 300, "demilight": 350,
           "regular": 400, "normal": 400, "medium": 500, "semibold": 600,
           "bold": 700, "extrabold": 800, "black": 900, "heavy": 900}


def parse_weight(text):
    """Returns a weight from a number or a name like `bold`."""
    t = text.strip().lower().replace("-", "").replace(" ", "")
    if t in WEIGHTS:
        return WEIGHTS[t]
    w = int(t)                          # ValueError for anything else
    if not 1 <= w <= 1000:
        raise ValueError(text)
    return w


def set_weight(font, tt, weight):
    """Sets a variable font's weight, or checks a static font's. Returns
    the weight the glyphs will have."""
    if "fvar" in tt:
        axes = tt["fvar"].axes
        values = [a.defaultValue for a in axes]
        tags = [a.axisTag for a in axes]
        if "wght" not in tags:
            if weight is not None:
                sys.exit("error: this variable font has no weight axis")
            return 400
        k = tags.index("wght")
        if weight is None:
            return round(values[k])
        a = axes[k]
        if not a.minValue <= weight <= a.maxValue:
            sys.exit(f"error: this font's weights are {a.minValue:g} to {a.maxValue:g}, "
                     f"not {weight}")
        values[k] = weight
        font.set_variation_by_axes(values)
        return weight
    actual = tt["OS/2"].usWeightClass if "OS/2" in tt else 400
    if weight is not None and weight != actual:
        sys.exit(f"error: this font has one weight, {actual}, not {weight}; use the file "
                 f"for that weight or a variable font (one with a weight axis)")
    return actual


RANGE = re.compile(r"^\s*U\+([0-9A-Fa-f]{1,6})(?:\s*-\s*U\+([0-9A-Fa-f]{1,6}))?\s*$")


def read_glyph_list(path):
    """Returns the code points in glyphs.txt, in order."""
    cps = []
    with open(path, encoding="utf-8-sig") as f:
        for line in f.read().splitlines():
            m = RANGE.match(line)
            if m:
                a = int(m.group(1), 16)
                b = int(m.group(2), 16) if m.group(2) else a
                cps += range(a, b + 1)
            else:
                cps += [ord(c) for c in line]
    return cps


# ---- rendering ---------------------------------------------------------------


class Glyph:
    def __init__(self, cp, w, h, xoff, yoff, advance, px):
        self.cp, self.w, self.h = cp, w, h
        self.xoff, self.yoff, self.advance = xoff, yoff, advance
        self.px = px
        self.codes = encode(px)

    def record(self):
        head = bytes([self.w, self.h, self.xoff, self.yoff, self.advance, 0])
        return head + b"".join(struct.pack("<H", c) for c in self.codes)


def render(font, cp):
    """Returns (bbox, advance, rows of 4-bit pixels) for a code point."""
    from PIL import Image, ImageDraw
    ch = chr(cp)
    advance = round(font.getlength(ch))
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    if x1 <= x0 or y1 <= y0:
        return (0, 0, 0, 0), advance, []
    img = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    w, h = img.size
    data = img.tobytes()
    rows = [[(data[y * w + x] * 15 + 127) // 255 for x in range(w)] for y in range(h)]
    # trim rows and columns that quantized to background
    while rows and not any(rows[0]):
        rows.pop(0); y0 += 1
    while rows and not any(rows[-1]):
        rows.pop(); y1 -= 1
    if not rows:
        return (0, 0, 0, 0), advance, []
    while not any(r[0] for r in rows):
        rows = [r[1:] for r in rows]; x0 += 1
    while not any(r[-1] for r in rows):
        rows = [r[:-1] for r in rows]; x1 -= 1
    return (x0, y0, x1, y1), advance, rows


def build(args):
    from PIL import ImageFont
    from fontTools.ttLib import TTFont
    font = ImageFont.truetype(args.font, args.size, index=args.face)
    tt = TTFont(args.font, fontNumber=args.face, lazy=True)
    cmap = tt.getBestCmap() or {}
    weight = set_weight(font, tt, args.weight)

    wanted, seen = [], set()
    skipped = {"duplicate": 0, "above U+FFFF": [], "not in the font": []}
    for cp in read_glyph_list(args.glyphs):
        if cp in seen:
            skipped["duplicate"] += 1
        elif cp > 0xFFFF:
            skipped["above U+FFFF"].append(cp)
        elif cp not in cmap:
            skipped["not in the font"].append(cp)
        else:
            wanted.append(cp)
        seen.add(cp)

    rendered = []
    for cp in wanted:
        rendered.append((cp,) + render(font, cp))
    # the cell: the font's ascent and descent, grown to hold every glyph
    ascent, descent = font.getmetrics()
    for cp, (x0, y0, x1, y1), adv, rows in rendered:
        if rows:
            ascent = max(ascent, -y0)
            descent = max(descent, y1)
    height = ascent + descent
    if height > 255:
        sys.exit(f"error: the cell is {height} pixels high, more than 255")

    glyphs, notes = [], []
    for cp, (x0, y0, x1, y1), adv, rows in rendered:
        w, h = x1 - x0, y1 - y0
        if x0 < 0:
            notes.append(f"U+{cp:04X} starts {-x0} pixel(s) left of its cell; moved right")
            x0 = 0
        if w > 255 or h > 255 or x0 > 255 or adv > 255:
            notes.append(f"U+{cp:04X} is too big for 8-bit sizes; skipped")
            continue
        px = [p for r in rows for p in r]
        glyphs.append(Glyph(cp, w, h, x0, ascent + y0 if rows else 0, adv, px))

    # fill to --max-bytes in priority order
    kept, dropped = [], []
    size = 12 + 514 + 2         # header, page table, padding
    for g in glyphs:
        need = 4 + len(g.record())
        if args.max_bytes and size + need > args.max_bytes:
            dropped.append(g.cp)
            continue
        kept.append(g)
        size += need
    data = write_font(kept, height, ascent, weight)
    check(data, kept)

    with open(args.out, "wb") as f:
        f.write(data)
    raw = sum((g.w * g.h + 1) // 2 for g in kept)
    packed = sum(2 * len(g.codes) for g in kept)
    print(f"{args.out}: {len(kept)} glyphs, weight {weight}, {len(data)} bytes, cell {height} pixels "
          f"(ascent {ascent}); pixels {packed} bytes, {raw / packed if packed else 0:.2f}x "
          f"smaller than plain 4-bit")
    for what, cps in skipped.items():
        if what == "duplicate":
            if cps:
                print(f"skipped {cps} duplicate(s)")
        elif cps:
            print(f"skipped {len(cps)} {what}: " + show(cps))
    if dropped:
        print(f"dropped {len(dropped)} that didn't fit in {args.max_bytes} bytes: " + show(dropped))
    for n in notes:
        print("note: " + n)
    if args.proof:
        proof(data, args.proof)
        print(f"{args.proof}: proof sheet")


def show(cps, limit=20):
    text = " ".join(f"U+{c:04X}" for c in cps[:limit])
    return text + (" ..." if len(cps) > limit else "")


# ---- the file ------------------------------------------------------------------


def write_font(glyphs, height, ascent, weight=400):
    """Header, page table, index and glyph records (see doc/fonts.md)."""
    glyphs = sorted(glyphs, key=lambda g: g.cp)
    n = len(glyphs)
    page = [0] * 257
    for p in range(257):
        page[p] = sum(1 for g in glyphs if (g.cp >> 8) < p)
    head = MAGIC + bytes([VERSION, 4, height, ascent]) + struct.pack("<HH", n, weight)
    table = b"".join(struct.pack("<H", v) for v in page) + b"\0\0"
    offset = len(head) + len(table) + 4 * n
    index, records = b"", b""
    for g in glyphs:
        rec = g.record()
        if offset >= 1 << 24:
            sys.exit("error: the font is over 16 MB")
        index += struct.pack("<I", ((g.cp & 255) << 24) | offset)
        records += rec
        offset += len(rec)
    return head + table + index + records


def lookup(data, cp):
    """Returns the byte offset of cp's glyph record, or None (as Forth would)."""
    p = cp >> 8
    lo, hi = struct.unpack_from("<HH", data, 12 + 2 * p)
    base = 12 + 514 + 2
    while lo < hi:                                  # binary search on the low byte
        mid = (lo + hi) // 2
        e = struct.unpack_from("<I", data, base + 4 * mid)[0]
        if (e >> 24) == (cp & 255):
            return e & 0xFFFFFF
        if (e >> 24) < (cp & 255):
            lo = mid + 1
        else:
            hi = mid
    return None


def read_glyph(data, offset):
    """Returns (w, h, xoff, yoff, advance, pixels) from a glyph record."""
    w, h, xoff, yoff, adv = data[offset:offset + 5]
    i, pixels = offset + 6, []
    while len(pixels) < w * h:          # codes until the box is full
        c, = struct.unpack_from("<H", data, i)
        i += 2
        pixels += decode([c], w * h)
    return w, h, xoff, yoff, adv, pixels[:w * h]


def check(data, glyphs):
    """Looks every glyph up and decodes it, as the device would."""
    for g in glyphs:
        off = lookup(data, g.cp)
        if off is None:
            sys.exit(f"internal error: U+{g.cp:04X} not found")
        w, h, xoff, yoff, adv, px = read_glyph(data, off)
        if (w, h, xoff, yoff, adv, px) != (g.w, g.h, g.xoff, g.yoff, g.advance, g.px):
            sys.exit(f"internal error: U+{g.cp:04X} doesn't decode")


def proof(data, path, columns=16, gap=2):
    """Draws every glyph in the file, decoded, on a grid."""
    from PIL import Image, ImageDraw
    n, = struct.unpack_from("<H", data, 8)
    height = data[6]
    base = 12 + 514 + 2
    entries = [struct.unpack_from("<I", data, base + 4 * i)[0] for i in range(n)]
    cells = [read_glyph(data, e & 0xFFFFFF) for e in entries]
    cw = max([max(c[4], c[2] + c[0]) for c in cells] + [1])
    rows = (n + columns - 1) // columns
    img = Image.new("RGB", (columns * (cw + gap) + gap, rows * (height + gap) + gap), (40, 40, 60))
    draw = ImageDraw.Draw(img)
    for k, (w, h, xoff, yoff, adv, px) in enumerate(cells):
        ox = gap + (k % columns) * (cw + gap)
        oy = gap + (k // columns) * (height + gap)
        draw.rectangle([ox, oy, ox + max(adv, 1) - 1, oy + height - 1], fill=(0, 0, 0))
        for y in range(h):
            for x in range(w):
                v = px[y * w + x] * 17
                if v:
                    img.putpixel((ox + xoff + x, oy + yoff + y), (v, v, v))
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(path)


# ---- self test -----------------------------------------------------------------


def selftest():
    import random
    rnd = random.Random(1)
    cases = [[], [WHITE], [BLACK], [7], [WHITE] * 600, [BLACK] * 600, [3] * 20,
             [WHITE] * 130 + [BLACK] * 130 + [5, 9] + [WHITE] * 40,
             [8] + [BLACK] * 520 + [8, 9, 10, 0, 0, 0, 0, 15]]
    for _ in range(3000):
        n = rnd.randrange(1, 400)
        px, c = [], WHITE
        while len(px) < n:
            r = rnd.random()
            if r < 0.5:
                px += [c] * rnd.randrange(1, 200)
                c = BLACK if c == WHITE else WHITE
            else:
                px += [rnd.randrange(16) for _ in range(rnd.randrange(1, 5))]
        cases.append(px[:n])
    for px in cases:
        codes = encode(px)
        if decode(codes, len(px)) != px:
            sys.exit(f"selftest FAILED on {px[:40]}...")
        if any(not 0 <= c <= 0xFFFF for c in codes):
            sys.exit("selftest FAILED: a code is not 16 bits")
    # a run-length check: long runs take few codes
    assert len(encode([9] + [BLACK] * 7 + [4] + [WHITE] * 7)) == 1
    assert len(encode([7] + [WHITE] * 511)) == 1
    assert len(encode([1, 2, 3, 0, 0, 0])) == 1
    print(f"mkfont selftest passed ({len(cases)} cases)")


# ---- command line --------------------------------------------------------------


class Args:
    font = None; face = 0; size = None; out = "font.lfnt"; weight = None
    max_bytes = 0; proof = None; glyphs = None


def main(argv):
    if "--selftest" in argv:
        selftest()
        return 0
    a = Args()
    it = iter(argv)
    try:
        for arg in it:
            if arg in ("-h", "--help"):
                print(__doc__)
                return 0
            elif arg == "-f":
                a.font = next(it)
            elif arg == "--face":
                a.face = int(next(it))
            elif arg == "-s":
                a.size = int(next(it))
            elif arg == "-w":
                a.weight = parse_weight(next(it))
            elif arg == "-o":
                a.out = next(it)
            elif arg == "--max-bytes":
                a.max_bytes = int(next(it))
            elif arg == "--proof":
                a.proof = next(it)
            elif arg.startswith("-"):
                raise ValueError(arg)
            else:
                a.glyphs = arg
    except (StopIteration, ValueError):
        print("usage: mkfont.py -f font.otf -s size [-w weight] [-o out.lfnt] [--face N] "
              "[--max-bytes N] [--proof sheet.png] glyphs.txt", file=sys.stderr)
        return 2
    if not (a.font and a.size and a.glyphs):
        print("usage: mkfont.py -f font.otf -s size [-w weight] [-o out.lfnt] [--face N] "
              "[--max-bytes N] [--proof sheet.png] glyphs.txt", file=sys.stderr)
        return 2
    build(a)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
