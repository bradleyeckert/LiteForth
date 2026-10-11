#!/usr/bin/env python3
"""Check LiteForth block source against doc/coding-rules.md.

Usage: lintf.py file.f ...

Reads source files in putblocks.py form (`( BLOCK n )` and `( BLOCK++ )`
markers, text after `( BLOCK IGNORE )` skipped) and reports, one line each:

    file:line: [block n row r] RULE message

B1  title row: `( Title ___ m/d/yy INI )`, the date at column 64
B3  a block ends with `-->` but block n+1 isn't in the file
B5  tab characters
H1  a header without a `\\` description on its row, or two names on one row
H2  columns: stack picture at 12, code at 28, `\\` at 80
H3  a colon definition without a stack picture, or a picture without `--`
    or without spaces inside its parentheses
H4  a description that starts with a capitalized word or ends with a period
--  rows longer than 128 bytes, trailing spaces

Columns count from 0 and are display columns: an East Asian wide character
takes 2. The 128-byte limit counts UTF-8 bytes, as blocks do.

Headers are found by a light reading of the source: `:` names a word, and
outside definitions so do create, variable, constant, bits, wordlist users,
label and any colon word whose body uses one of them (value, task,
vocabulary, ...). Text that `(`, `.(`, `\\`, `."` and the like parse is
skipped, as is the word after `'`, `postpone`, `char` and `[char]`.

Exit status: 0 if nothing was reported, 1 otherwise, 2 for usage errors.
"""
import re
import sys
import unicodedata

ROW_BYTES = 128
COL_PICTURE = 12
COL_CODE = 28
COL_DESC = 80
COL_DATE = 64

MARKER = re.compile(r"^\(\s*BLOCK(?:\s+(\d+|IGNORE)|(\+\+))\s*\)")
TITLE = re.compile(r"^\( \S.*? _+ (\d{1,2}/\d{1,2}/\d{2}) [A-Z]{2,4} \)$")

DEFINERS = {"create", "variable", "constant", "bits", "label"}
# Words that parse text up to a delimiter, and whether they do so when
# compiling (immediate) as well as when interpreting.
PARSERS = {"(": (")", True), ".(": (")", True), '."': ('"', True),
           's"': ('"', True), 'abort"': ('"', True), '_,"': ('"', False)}
SKIP_NEXT = {"'", "x'", "[']", "postpone", "char", "[char]"}


def width(s):
    """Display width of s: East Asian wide and fullwidth characters are 2."""
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


class Row:
    """One row of a block, split into tokens with display columns."""

    def __init__(self, text):
        self.text = text
        self.tokens = []                # (column, index in text, token)
        for m in re.finditer(r"\S+", text):
            self.tokens.append((width(text[:m.start()]), m.start(), m.group()))

    def col(self, index):
        return width(self.text[:index])


class Linter:
    def __init__(self, path):
        self.path = path
        self.problems = 0
        self.definers = set(DEFINERS)
        self.compiling = False
        self.body = []                  # tokens of the colon definition being read
        self.defining = None            # its name

    def report(self, lineno, block, row, rule, msg):
        self.problems += 1
        print(f"{self.path}:{lineno}: [block {block} row {row}] {rule} {msg}")

    def run(self):
        with open(self.path, encoding="utf-8-sig") as f:
            lines = f.read().split("\n")
        blocks = {}                     # block -> [(lineno, text)]
        current = None
        last = None                     # the last block started, for `++`
        for lineno, text in enumerate(lines, 1):
            text = text.rstrip("\r")
            m = MARKER.match(text)
            if m:
                if m.group(1) == "IGNORE":
                    current = None
                elif m.group(2):
                    current = None if last is None else last + 1
                else:
                    current = int(m.group(1))
                if current is not None:
                    blocks[current] = []
                    last = current
                continue
            if current is not None:
                blocks[current].append((lineno, text))
        for n, rows in blocks.items():
            self.block(n, rows, n + 1 in blocks)
        return self.problems

    def block(self, n, rows, has_next):
        last = None
        for r, (lineno, text) in enumerate(rows):
            self.basics(lineno, n, r, text)
            if r == 0:
                self.title(lineno, n, text)
                continue
            self.code_row(lineno, n, r, Row(text))
            if text.strip():
                last = (lineno, r, text)
        if last and last[2].split()[-1] == "-->" and not has_next:
            self.report(last[0], n, last[1], "B3",
                        f"ends with --> but block {n + 1} isn't in this file")

    def basics(self, lineno, n, r, text):
        size = len(text.encode("utf-8"))
        if size > ROW_BYTES:
            self.report(lineno, n, r, "--", f"row is {size} bytes, more than {ROW_BYTES}")
        if text != text.rstrip():
            self.report(lineno, n, r, "--", "trailing spaces")
        if "\t" in text:
            self.report(lineno, n, r, "B5", "tab character")

    def title(self, lineno, n, text):
        m = TITLE.match(text)
        if not m:
            self.report(lineno, n, 0, "B1", "title row isn't `( Title ___ m/d/yy INI )`")
        elif width(text[:m.start(1)]) != COL_DATE:
            self.report(lineno, n, 0, "B1",
                        f"title date at column {width(text[:m.start(1)])}, not {COL_DATE}")

    def code_row(self, lineno, n, r, row):
        """Walks the row's tokens, finds the headers it makes and its description."""
        toks = row.tokens
        headers = []                    # (kind, token index of the name)
        desc = None                     # (column, text index of the `\`)
        i = 0
        while i < len(toks):
            col, idx, t = toks[i]
            low = t.lower()
            if low == "\\":
                desc = (col, idx)
                break
            if low in PARSERS and (PARSERS[low][1] or not self.compiling):
                end = row.text.find(PARSERS[low][0], idx + len(t) + 1)
                if end < 0:
                    break
                while i < len(toks) and toks[i][1] <= end:
                    i += 1
                continue
            if self.compiling:
                self.body.append(low)
                if low == ";":
                    self.end_definition()
                elif low == "[":
                    self.compiling = False
                elif low in SKIP_NEXT and i + 1 < len(toks):
                    self.body.append(toks[i + 1][2].lower())
                    i += 1
                i += 1
                continue
            if low == "]":
                self.compiling = True
            elif low == ":noname":
                self.compiling, self.body, self.defining = True, [], None
            elif low == ":" and i + 1 < len(toks):
                headers.append((":", i + 1))
                self.compiling, self.body, self.defining = True, [], toks[i + 1][2].lower()
                i += 2
                continue
            elif low in self.definers and i + 1 < len(toks):
                headers.append((low, i + 1))
                i += 2
                continue
            elif low in SKIP_NEXT:
                i += 1
            i += 1
        self.check_headers(lineno, n, r, row, headers, desc)

    def end_definition(self):
        self.compiling = False
        if self.defining and any(w in self.definers or w == ":" for w in self.body):
            self.definers.add(self.defining)
        self.body, self.defining = [], None

    def check_headers(self, lineno, n, r, row, headers, desc):
        if len(headers) > 1:
            names = ", ".join(row.tokens[h[1]][2] for h in headers)
            self.report(lineno, n, r, "H1", f"names on one row: {names}")
        for kind, ti in headers:
            name_col, name_idx, name = row.tokens[ti]
            if desc is None:
                self.report(lineno, n, r, "H1", f"`{name}` has no \\ description")
            else:
                self.check_description(lineno, n, r, row, name, desc)
            if kind == ":":
                self.check_picture(lineno, n, r, row, ti, desc)

    def check_description(self, lineno, n, r, row, name, desc):
        col, idx = desc
        before = row.text[:idx].rstrip()
        if col != COL_DESC and not (col > COL_DESC and width(before) >= COL_DESC - 1):
            self.report(lineno, n, r, "H2", f"`{name}` description at column {col}, not {COL_DESC}")
        text = row.text[idx + 1:].strip()
        if not text:
            self.report(lineno, n, r, "H4", f"`{name}` has an empty description")
            return
        first = text.split()[0]
        if re.match(r"[A-Z][a-z]", first):
            self.report(lineno, n, r, "H4", f"`{name}` description starts with a capital")
        if text.endswith(".") and not text.endswith(".."):
            self.report(lineno, n, r, "H4", f"`{name}` description ends with a period")

    def check_picture(self, lineno, n, r, row, ti, desc):
        toks = row.tokens
        name_col, name_idx, name = toks[ti]
        if ti + 1 >= len(toks) or toks[ti + 1][2] != "(" or (desc and toks[ti + 1][1] > desc[1]):
            self.report(lineno, n, r, "H3", f"`{name}` has no stack picture after the name")
            return
        pic_col, pic_idx, _ = toks[ti + 1]
        name_end = name_col + width(name)
        if pic_col != COL_PICTURE and not (name_end >= COL_PICTURE - 1 and pic_col == name_end + 1):
            self.report(lineno, n, r, "H2", f"`{name}` stack picture at column {pic_col}, not {COL_PICTURE}")
        # the picture is one or more ( ... ) groups
        end_text = desc[1] if desc else len(row.text)
        pos = pic_idx
        groups = []
        while pos < end_text and row.text[pos] == "(":
            close = row.text.find(")", pos)
            if close < 0 or close > end_text:
                break
            if groups and "--" not in row.text[pos:close + 1]:
                break                   # a comment after the picture, not part of it
            groups.append(row.text[pos:close + 1])
            pos = close + 1
            while pos < end_text and row.text[pos] == " ":
                pos += 1
        for g in groups:
            if "--" not in g:
                self.report(lineno, n, r, "H3", f"`{name}` picture `{g}` has no --")
            if not g.endswith(" )"):
                self.report(lineno, n, r, "H3", f"`{name}` picture `{g}` needs a space before )")
        code = row.text[pos:end_text].rstrip()
        if not code:
            return
        code_col = row.col(pos)
        pic_end = row.col(len(row.text[:pos].rstrip()))
        if code_col < COL_CODE:
            # allowed only when the code wouldn't fit before the description at 28
            if COL_CODE + width(code) < COL_DESC:
                self.report(lineno, n, r, "H2", f"`{name}` code at column {code_col}, not {COL_CODE}")
        elif code_col > COL_CODE and pic_end < COL_CODE - 1:
            self.report(lineno, n, r, "H2", f"`{name}` code at column {code_col}, not {COL_CODE}")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0 if argv else 2
    problems = 0
    for path in argv:
        try:
            problems += Linter(path).run()
        except OSError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
