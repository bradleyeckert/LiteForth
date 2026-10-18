# Scripts

Python tools for working with LiteForth's block files and fonts on the PC.
They need Python 3; `mkfont.py` also needs Pillow and fontTools, and
`sealblocks.py` the `cryptography` package. The
examples use `py`, the Windows launcher; on Linux and macOS use `python3`.
Each script prints its full usage with `-h`.

| Script | What it does |
|---|---|
| `putblocks.py` | Writes Forth source into blocks |
| `getblocks.py` | Dumps blocks back out as source text; also the `git diff` driver for `.fb4` files |
| `putbin.py` | Writes a binary file, such as a font, into blocks |
| `mkfont.py` | Makes 4-bit anti-aliased bitmap fonts for the graphic LCDs |
| `lintf.py` | Checks Forth source against `doc/coding-rules.md` |
| `mkexfat.py` | Builds small exFAT card images (with `lfblocks.fb4` inside) for testing; `lf -k card.img` uses one as the board uses its card |
| `sealblocks.py` | Makes and checks sealed (encrypted, signed) block files for updates, and their keys |
| `lfmcp.py` | Gives an AI assistant (an MCP client such as the Claude desktop app) LiteForth's terminal on a serial port |

The other files here aren't Python: `regression.f` is the test script that
`make test` runs, and `settings.json` has Windows Terminal key bindings for
booting `lf` (F9) and running the regression tests (F12).

## Block files

A block file (`lfblocks.fb4` by default) holds 4 KB blocks, block n at
byte n × 4096. Block 0 starts with the signature `LITEFORTH` and is never
written by these scripts. The scripts need an existing signed file, and
extend it as needed. The `lfblocks.fb4` on a LiteForth microSD card is
such a file, so they can write to it in place (`F:\lfblocks.fb4`).

## putblocks.py

```
py putblocks.py [-n] source.f [blockfile]
```

Splits the source at `( BLOCK n )` lines and writes each part as block n,
32 rows of 128 columns padded with spaces. `( BLOCK++ )` starts the block
after the last one, and `( BLOCK IGNORE )` skips the lines up to the next
marker. A line over 128 bytes, a block over 32 lines, or a block given
twice is an error, and then nothing is written. `-n` shows what would be
written. `forth/build.bat` uses it to write the system source into
`bin/lfblocks.fb4`:

```
cd forth
py ../scripts/putblocks.py core.f ../bin/lfblocks.fb4
```

On the board, `0 ymodem` does the same with a file sent over the
terminal (kwai: `~x core.f`), and `100 ymodem` does what `putbin.py`
does.

## getblocks.py

```
py getblocks.py [-0] [-o out.f] [blockfile]
```

The reverse: writes every non-blank block as a `( BLOCK n )` marker and its
rows, so `putblocks.py` can write the dump back unchanged. A binary block
(from `putbin.py`) becomes one `( BLOCK IGNORE ) block n: binary, crc32 ...`
line, which `putblocks.py` skips. `-0` includes block 0's header text.

To make `git diff` show block files as text, run this once in your clone:

```
git config diff.fb4.textconv "py scripts/getblocks.py -0"
```

## mkfont.py

```
py mkfont.py -f font.otf -s size [options] glyphs.txt
py mkfont.py --list [words]
```

Renders the glyphs in `glyphs.txt` from a TrueType or OpenType font (such
as Noto Sans CJK) at `size` pixels and writes a compressed `.fnt` file with
an index by code point. `glyphs.txt` is UTF-8: a line like `U+0020-U+007E`
adds a range, and any other line adds its characters. Put the most-used
glyphs first; the order is the priority.

| Option | Meaning |
|---|---|
| `-f FILE` | The font. A bare name is also looked for in the installed font folders. Give `-f` again for fallback fonts (Arabic, Thai, ...) |
| `--face N` | The face of a `.ttc` collection, for the `-f` before it |
| `-s PIXELS` | The size |
| `-w WEIGHT` | The weight: 100 to 900, or `regular`, `bold`, ... |
| `-o FILE` | The output (default `font.fnt`) |
| `--blocks` | Keep each glyph inside one 4 KB block, for reading from blocks |
| `--max-bytes N` | Stop adding glyphs at this size |
| `--proof FILE` | Also draw the decoded glyphs as a PNG |
| `--list` | List the installed fonts and the `-f`/`--face` they need |

```
py mkfont.py --list noto cjk
py mkfont.py -f NotoSansCJKsc-Regular.otf -s 24 --blocks -o cjk24.fnt --proof cjk24.png glyphs.txt
```

`fonts/build.bat` builds the project's fonts this way. If Pillow or
fontTools is missing, it prints the command to install them for the right
Python (`py -m pip install pillow fonttools`). The file format is in
`doc/fonts.md`.

## putbin.py

```
py putbin.py [-n] file.fnt block [blockfile]
```

Writes a binary file into blocks as one stream starting at byte 32 of
`block` (the first 32 bytes are left as zeros for an HMAC). It prints the
blocks used and the next free block, where the next file can go, and warns
if a font has glyphs crossing a block boundary (build it with `--blocks`).

```
py putbin.py cjk24.fnt 100 ../bin/lfblocks.fb4
cjk24.fnt: 108824 bytes at byte 32 of block 100: blocks 100-126, next free block 127
```

## lintf.py

```
py lintf.py file.f ...
```

Reports each departure from `doc/coding-rules.md` on its own line: title
rows, the stack picture and `\` description columns of each definition,
missing descriptions, tabs, long rows and trailing spaces. The exit status
is 1 if it reported anything.

```
py lintf.py ../forth/core.f
```

## Tests

Each script has a test suite under `src/target/desktop/unit_tests/`
(`putblocks`, `putbin`, `mkfont`, `lintf`), run by `make test`.

## lfmcp.py

An MCP server (Model Context Protocol, over stdin/stdout) that lets an AI
assistant use LiteForth's terminal on a serial port, such as the
CH32H417's USB CDC port. It has three tools:

| Tool | What it does |
|---|---|
| `forth` | Sends one or more lines and returns what LiteForth printed, each line shown as `> line` and followed by its output, waiting for `ok>` (5 s by default) |
| `forth_read` | Listens for a while without sending anything, for a running app's output |
| `forth_restart` | Sends Ctrl+X three times: on the CH32H417, restarts LiteForth without starting the app |

It opens the port for each call and closes it after, so kwai can use the
port in between, but not at the same time (Windows lets one program open
a COM port; a call then says the port is in use). It needs no packages:
it uses Win32 through `ctypes` on Windows and `termios` elsewhere.

For the Claude desktop app on Windows, add it to
`%APPDATA%\Claude\claude_desktop_config.json` and restart the app:

```json
{
  "mcpServers": {
    "liteforth": {
      "command": "py",
      "args": ["C:\\Users\\User\\Documents\\GitHub\\LiteForth\\scripts\\lfmcp.py", "COM17"]
    }
  }
}
```

If the assistant can share a folder with your PC but can't start an MCP
server (a cloud session linked to your computer), use the mailbox instead:

```
py scripts\lfmcp.py COM17 --mailbox
```

It watches `mailbox/` in the repo (ignored by git) for request files
`NAME.json`, such as `{"tool": "forth", "arguments": {"text": "1 2 + ."}}`,
answers each in `NAME.txt` and deletes the request. Ctrl+C stops it.

`unit_tests/lfmcp` tests both modes against the desktop `lf` over a pair
of ptys.

## sealblocks.py

```
py sealblocks.py root-key root
py sealblocks.py release-key root.key 1 release1.key --product 0x4C46
py sealblocks.py seal lfblocks.fb4 sealed.fb4 --release release1.key --boards boards.csv --revision 1
py sealblocks.py check sealed.fb4 --root root.pub
py sealblocks.py open sealed.fb4 opened.fb4 --root root.pub --board-key board.key
py sealblocks.py lf-board board.lfb --board-key board.key --root root.pub --product 0x4C46
py sealblocks.py root-header root.pub --product 0x4C46 -o ..\src\target\CH32H417\V5F\User\sealed_root.h
```

Makes a sealed block file from a plain one, as planned in
[doc/encrypted-blocks.md](../doc/encrypted-blocks.md): the content blocks
encrypted (AES-128 or SM4 in CTR mode) with a new key for each release,
that key wrapped for each board in `boards.csv` (`UID,public key,name` in
hex), the block hashes in hash table blocks after the content, and block 0
signed by a release key that the root key certifies. `check` checks
everything that needs no board key; `open` also unwraps and decrypts, as a
board would, and refuses what a board would refuse (`--min-revision`,
`--min-serial`, `--product`). `board-key` makes a board key pair on the PC
for testing; a real board will make its own. `lf-board` writes a board
file for the desktop `lf -K`, which then reads the sealed file as that
board would, and `root-header` the `sealed_root.h` a CH32H417 build reads
them with. On the board, `enroll` prints its `boards.csv` line.

Key files are JSON; `.key` files hold private keys. Keep the root's
offline: `release-key` is the only command that needs it. Needs the
`cryptography` package (`py -m pip install cryptography`).
`unit_tests/seal` tests it: round trips with both ciphers, the layout
against the formulas in the plan, and each refusal.
