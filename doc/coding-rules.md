# LiteForth coding rules

These rules are for Forth source in `forth/`: `core.f` and the files that
build the rest of the block file. They start from Leo Brodie's conventions
in *Thinking Forth* (appendix E) and the stack notation of the Forth-2012
standard (section 2.2), adapted to what `core.f` already does.

The reason for most of them is that **the source is the glossary**. Every
header records the block and row it was defined on, so a tool can show the
definition's line on request. If that line carries the stack picture and a
one-line description, the glossary entry costs nothing to keep up to date.

## 1. Where a word's documentation comes from

A header's `aux` field holds:

| Bits | Contents |
|---|---|
| 31:28 | Flags: `A_SMUDGED`, `A_IMMEDIATE`, `A_CONSTANT`, `A_UNRESOLVED` |
| 27:5 | Block number (`BLK` when the header was made) |
| 4:0 | Row in that block (`>IN` / 128 just after the name was parsed) |

So `aux >> 5 & 0x7FFFFF` is the block and `aux & 31` the row. A lookup
tool takes that row of that block and shows:

- the stack picture: the first `( ... )` after the name, plus any
  `( R: ... )` or `( C: ... )` right after it;
- the description: the text after the first `\` that follows the
  stack picture.

**Block 0 means "no source".** Words compiled from the terminal, or from a
file piped into `lf` (`lf < core.f`), have `BLK` = 0. To get real
locations, write the file into blocks with `putblocks.py` and `load` it.

**Built-in words** (the C tables in `forth.c`) have their `aux` set by
hand to `32 * block + row`. That row is in `forth/block2_6.f`
(blocks 2 to 6), whose rows are written in this format:

```
( x1 x2 -- x2 x1 )              \ Exchange the top two stack items.
```

Row 0 of a block is its title, so indices that are multiples of 32
(96, 128, 160, 192) are never used. When you add a built-in word:

- give it the next free index;
- put its row at the same position in `block2_6.f`;
- keep the block titles ("lines 129 to 159") in step.

## 2. The header row

**H1. One row says it all.** The defining word, the name, the stack
picture and the description are on one row: the row where the name is
parsed. Code may continue on later rows. The description must not.

**H2. Columns** (counted from 0, as in `core.f`; a wide character such as
CJK counts 2, as an editor shows it):

| Column | Contents |
|---|---|
| 0 | `:` and the name (or the defining phrase, e.g. `_udata variable hld`) |
| 12 | Stack picture |
| 28 | Code |
| 80 | `\` and the description, which runs to column 127 at most |

A row is at most 128 bytes, and `putblocks.py` rejects longer ones. When
a field is too long for its column, the next field follows it after one
space (`: definitions ( -- )`), but the description still starts at
column 80 if the code ends before it. Code may start left of column 28
only when it wouldn't fit between 28 and 79.

```
: here      ( -- a )        'here @ ;                                           \ address of first free cell or slice
: >body     ( xt -- a )     0 swap caddr  begin  @+     ( acc a inst )          \ extract body address from code
    dup E000 and _lit = if 1FFF and nip swap 0D lshift + exit then
```

**H3. Stack pictures** use Forth-2012 notation:

- Item names: `x` any cell, `n` signed, `u` unsigned, `d` and `ud`
  doubles, `flag`, `c` or `char`, `xt`, `wid`, `ior`, `a` or `addr`,
  `a-addr` (cell aligned), `ca` (character address).
- Show the return stack with `( R: before -- after )` and compile-time
  behavior with `( C: before -- after )`.
- Show parsed text as `<name>` or `"<spaces>name"`.
- `|` separates alternative results, e.g. `( n1 -- | n1 )` for a word
  that may exit its caller.
- Number repeated items: `n1 n2 -- n3`. A primed name means the same item
  changed: `a len -- a' len'`.

**H4. The description** is a short lowercase phrase with no closing
period. It says what the word does, not how, e.g. `\ trim trailing blank
bytes`. The glossary shows only this row, so the phrase must make sense
alone.

**H5. Every word gets a description,** even the obvious ones (`1+`,
`max`). For internal `_` words, a few words are enough.

**H6. One name per row.** Two headers on one row share a glossary entry.
Data definitions get their own rows and descriptions. The stack picture
may be left out when the defining word already implies it (`variable`,
`constant`, `value`):

```
_udata variable hld                                                             \ next free position in numbuf
_udata create numbuf 10 allot                                                   \ pictured numeric output buffer
```

**H7. Names made later.** A header made by a defining word records the
row where that word runs: `20 32 32 task t1` documents `t1`. Give such
rows a description too. For `:noname ... ; constant awake`, the
description goes on the `; constant awake` row (or define a named word
instead).

## 3. Blocks and files

**B1. Title row.** Row 0 is `( Title ___ m/d/yy INI )`. Pad with
underscores so that the date starts at column 64, and end with your
initials. Update the date when you change the block.

**B2. One topic per block.** Rows 1 to 31 hold code. A topic that needs
more blocks says so in its titles: `Multitasking 1/2`, `Multitasking 2/2`.

**B3. Chains.** A block that is part of a chain ends with `-->`. The last
block of a chain doesn't.

**B4. Leave things as you found them.** Each block restores what it
changes before it ends:

- the base: `hex ... decimal`;
- the data section: `_udata` is selected at the end of a block (a
  `variable` in `_text` space corrupts headers);
- the search order and `current`: `also editor definitions ... previous
  definitions`.

**B5. Source files** are dual-use: `( BLOCK n )` markers for
`putblocks.py`, and text for the shell only after `( BLOCK IGNORE )`.

- Use no tabs: columns are bytes in a 128-column row.
- Write in UTF-8, and keep `core.f`'s BOM.
- Put hex digits in uppercase (`3FFFFF`).

**B6. Block map** (from `core.f`):

| Block | Use |
|---|---|
| 0 | Signature and metadata |
| 1 | Home block |
| 2 to 5 | Glossary of the built-in words |
| 6 to 19 | Core lexicon |
| 25 and up | Application |

## 4. Names

Write names in lowercase. If Forth-2012 has a word for the behavior, use
its standard name (`compile,`, not `,compile`). Otherwise keep names short
and pronounceable, and use these patterns, Brodie's plus LiteForth's own:

| Pattern | Meaning | Examples |
|---|---|---|
| `_name` | Internal factor or run-time part, not for applications | `_section`, `_again`, `_does` |
| `,name` | Compile a *name* into a section | `,inst`, `,jump` |
| `.name` | Print | `.map`, `.wid` |
| `name?` | Return a flag | `key?`, `emit?` |
| `name!` / `name@` | Store / fetch | `base!`, `u!` |
| `>name` / `name>` | Convert to, or move to / from | `>body`, `>r`, `r>` |
| `'name` | Address of | `'here` |
| `name+` | Advance | `slice+`, `chars+`, `@+` |
| `-name` | Reverse, or the opposite sense | `-trailing`, `-if` |
| `[name]` | Compile-time version, immediate | `[char]` |
| `\|name\|` | Size of | `\|context\|`, `\|block\|` |
| `name[` ... `]name` | Bracket pair | `shft[ ]shl`, `task[ ]task` |
| `u`, `d`, `s` prefixes | Unsigned, double, signed | `u.r`, `dabs`, `s@` |
| `n` + `parm(s)` | Exit the caller unless there are items to work on | `1parm`, `2parms` |

## 5. Factoring and the stacks

**F1. Short definitions.** Most definitions fit on their header row. Up
to about five rows is fine when the logic needs it (`multitask`). Factor
anything longer.

**F2. Factor repeated phrases** into `_` words rather than copying them.

**F3. Keep at most three items in play** on the data stack. Forth has no
`pick` or `roll` here, so use the A and B registers for addresses.

**F4. Balance the return stack** within a definition. The exception is a
word that exits its caller on purpose (`1parm`, `2parms`), which shows it
with `|` in its stack picture.

**F5. Stack comments in code.** After any step where the stack isn't
obvious, add `( a n )` showing what is on it. Place it inline or toward
column 79, as `multitask` does.

**F6. Spacing:**

- One space between words and two between phrases:
  `chere iaddr  over iaddr inv +  swap`.
- Continuation rows indent 4 spaces, or line up under column 28 for a
  short tail.
- A multi-row definition ends with `;` on its own row. A one-row
  definition ends with `;` on that row.
- `immediate` goes after `;` on the same row.

## 6. LiteForth specifics

**M1. A and B are scratch registers.** `@`, `!`, `,`, `@+` and `+!` use
A, and `rot` and `cmove` use B. Assume that any call to another word
clobbers both unless its description says otherwise. Within a word, A and
B used as pointers (`@a+`, `!b+`) are the fast way to walk memory.

**M2. Addresses carry metadata** in their upper bits (slice size, byte
position). Do address arithmetic with the words for it (`amask`,
`chars+`, `char-`, `slice+`, `bytepos`), not with raw `+` and `and`.

**M3. Errors** are `yeet` with a standard negative code from
`src/errcodes.h`, using the branch-free idiom
`<condition> 0<> <code> and yeet`:

```
dup stack-masks inv and 0<> -118 and yeet
```

**M4. Sections.** Data goes in `_udata` (RAM, not initialized) by default.
Use `_idata` for initialized data, which `save-idata` keeps, and `_text`
for read-only tables. Select the section before `variable`, `create` or
`,`, and go back to `_udata` afterwards.

**M5. Loops and flags.**

- `n for ... next` runs n times, but `0 for` runs 2^32 times, so guard
  with `1parm` / `2parms` / `3parms` when n can be 0.
- `-if` tests the sign and keeps the value, while `if` consumes its flag.
- True is -1.

**M6. Size and speed.**

- Fold constant expressions at compile time with `[ ... ] literal`.
- A literal like `base` costs 3 instructions, so a word used often
  (`base!`) can pay for itself.
- Judge changes by the code they compile, which `see` shows.

## 7. Checking

`scripts/lintf.py forth/*.f` checks the mechanical rules: B1, B3, B5,
H1 to H4, and rows longer than 128 bytes or with trailing spaces. It
reports each problem as `file:line: [block n row r] RULE message` and
exits with 1 if it found any. The rest (correct pictures, good names,
factoring) is for review.

## 8. Tests

New words get `T{ ... -> ... }T` assertions in `scripts/regression.f`.
That file carries copies of the `core.f` words it tests, so when you
change one of those words, update its copy too.
