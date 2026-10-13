# LiteForth Editor Lexicon

In LiteForth, code is organized into **4096-byte blocks (screens)** of 32 lines of 128 characters each.
Editing within LiteForth is done with the **`editor` vocabulary** (blocks 18 to 20 of `forth/core.f`),
a compact lexicon of single-letter and short commands for line- and character-level changes.

The search order is normally `[forth forth]` (`order` shows it). `editor` makes it
`[editor forth]`, so the editor words come first and the rest of Forth is still there, and `forth`
makes it `[forth forth]` again. Names are not case sensitive, so `EDIT` and `edit` are the same word.

The editor works on the **current block** (`scr`), a **current line** (0 to 31) and a **cursor**,
a column in the current line. Commands that change the block write it to storage straight away
(`update save-buffers`), because the terminal empties the block buffers after every line it
interprets.

---

## 1. Screen Navigation & Management

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`editor`** | `( -- )` | Sets the search order to `[editor forth]`. |
| **`edit`** | `( n -- )` | Lists block `n`, makes it the current block, and puts the cursor at the start of line 0. |
| **`list`** | `( n -- )` | Lists block `n` with line numbers (0 to 31) and makes it the current block. |
| **`l`** | `( -- )` | Lists the current block. |
| **`n`** | `( -- )` | Lists the **next** block (`n+1`) and makes it current. |
| **`b`** | `( -- )` | Lists the previous block (**back**, `n-1`) and makes it current. |
| **`fl`** | `( -- )` | **Flush**: saves modified buffers to storage and unassigns them. |
| **`index`** | `( u1 u2 -- )` | Shows the first line of blocks `u1` to `u2`. |

---

## 2. Line Operations

Lines are numbered `0` to `31`. Text is the rest of the input line after the command, with trailing
spaces removed. Text longer than the room on the line is cut off.

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`t`** | `( n -- )` | **Type** line `n` and make it the current line, with the cursor at its start. |
| **`p`** | `( "text" -- )` | **Put** text into the current line, replacing it. |
| **`i`** | `( "text" -- )` | **Insert** text as a new line at the current line, moving it and the lines below down. Line 31 is lost. |
| **`d`** | `( n -- )` | **Delete** line `n`, moving the lines below up. Line 31 becomes blank, and line `n` becomes current. |
| **`clear`** | `( -- )` | Fills the current block with spaces. |

---

## 3. Character & String Editing Within a Line

These work at the cursor. `t` puts it at the start of a line, and `f` puts it just after the
string it finds. The changed line is typed after each command.

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`f`** | `( "text" -- )` | **Find** text from the cursor to the end of the block. On a match, that line becomes current, the cursor moves past the match and `^` marks it. `f` with no text finds the last string again. |
| **`c`** | `( "text" -- )` | **Change** the string just found (it must end at the cursor) to text. |
| **`e`** | `( -- )` | **Erase** from the cursor to the end of the line. |
| **`r`** | `( "text" -- )` | **Replace** the rest of the line, from the cursor, with text. |
| **`s`** | `( n -- )` | **Spread**: insert `n` spaces at the cursor. Characters pushed past column 127 are lost. |
| **`x`** | `( n -- )` | **Xtract**: delete `n` characters at the cursor, closing up the line. |

---

## Example LiteForth Editor Session

```forth
ok>editor                   \ search order [editor forth]
ok>30 edit                  \ list block 30 and edit it
ok>3 t                      \ make line 3 current: ": square dup * ;"
ok>p : cube dup dup * * ;   \ put new code into line 3
ok>f dup                    \ find the first dup after the cursor
ok>c over                   \ change it: ": cube over dup * * ;"
ok>l                        \ list the block again
ok>forth                    \ back to [forth forth]
```
