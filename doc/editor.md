# LiteForth Editor Lexicon

In LiteForth, code is organized into standard **4096-byte blocks (screens)** containing 32 lines of 128 characters each.
Editing within LiteForth is performed using the **`EDITOR` vocabulary**, which provides a compact lexicon of
single-letter and short commands for line- and character-level manipulation.

Executing `EDITOR` adds these commands to the search order (or replaces `FORTH` in the context wordlist).
Typing `FORTH` switches back to the standard compilation/interpretation lexicon.

---

## 1. Screen Navigation & Management

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`EDITOR`** | `( -- )` | Switches search order context to the `EDITOR` vocabulary. |
| **`EDIT`** | `( n -- )` | Opens block `n` for editing, setting the current block and displaying it. |
| **`LIST`** | `( n -- )` | Lists block `n` with line numbers (0 to 15) without changing the current editor line. |
| **`N`** | `( -- )` | Advances to the **Next** block (`n+1`) and lists it. |
| **`B`** | `( -- )` | Moves back to the **Previous (Back)** block (`n-1`) and lists it. |
| **`FL`** | `( -- )` | **Flush** / Save modified buffers back to disk/storage. |

---

## 2. Line Operations

In LiteForth line editing, commands operate on line indices (`0` through `31`).

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`T`** | `( n -- )` | **Type** line `n`. Displays line `n` and sets it as the current line pointer. |
| **`L`** | `( -- )` | **List** current block. |
| **`P`** | `( text... -- )` | **Put** text into the current line (overwrites the line). |
| **`n P`** | `( text... -- )` | **Put** text directly into line `n`. |
| **`I`** | `( text... -- )` | **Insert** text as a new line above the current line (shifts line 31 off bottom). |
| **`D`** | `( n -- )` | **Delete** line `n`. Shifts subsequent lines up and fills line 31 with spaces. |
| **`CLEAR`** | `( -- )` | Clears all 32 lines in the current block buffer (fills with spaces). |

---

## 3. Character & String Editing Within a Line

When a line is selected with `T` or modified, string sub-lexicon commands allow fine-grained cursor and string replacements.

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| **`F`** | `( text... -- )` | **Find** target string in the current line or block. |
| **`C`** | `( text... -- )` | **Change** last found string to new target text. |
| **`E`** | `( -- )` | **Erase** / Delete from cursor position to end of line. |
| **`R`** | `( text... -- )` | **Replace** remaining line contents with new text. |
| **`S`** | `( n -- )` | **Spread** / Insert `n` spaces at cursor position. |
| **`X`** | `( n -- )` | **Xtract** / Delete `n` characters at cursor position. |

---

## Example LiteForth Editor Session

A typical workflow in a LiteForth interactive session looks like this:

```forth
ok>EDITOR                   \ Switch to EDITOR lexicon
ok>25 EDIT                  \ Load and view block 25
ok>3 T                      \ Target line 3: ": SQUARE DUP * ;"
ok>3 P : CUBE DUP DUP * * ; \ Put new code into line 3
ok>FL                       \ Save changes to disk
ok>FORTH                    \ Switch back to main FORTH lexicon
ok>
```