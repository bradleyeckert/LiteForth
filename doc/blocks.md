# Blocks in LiteForth

LiteForth uses blocks rather than files.

I know what you're thinking: *For God's sake, why?*

The desktop ecosystem is stream-based, with source code in text files.
Desktop Forth moved away from blocks. The MCU environment has no such ecosystem.

Charles H. Moore, the creator of Forth, has consistently advocated for raw blocks over operating system file systems throughout his career.
Chuck's Forths have always used modal editors, usable from the terminal, rather than cursor-based input.
VIM users can tell you there is a tradeoff between the speed of typing and the speed of cursor positioning.
A non-modal editor is not necessarily faster to use.

Files increase complexity without actually adding anything.
Blocks provide a clean way to order code for readability, since they control the load order.
As a concession to modern times, LiteForth blocks are 4 KB instead of 1 KB, with 32 rows of 128 columns.

The first line (Line 0) of a block is reserved for a comment line:
```forth
( Block Title / Summary                  mm/dd/yy initials )
```
A 4 KB block eliminates the need for shadow blocks and allows for 79 columns of code and 46 columns of comment.
32 rows means Row 0 consumes 3% rather than 6% of available space.

## Using Blocks

In the PC environment, `./scripts` contains Python scripts to convert back and forth between files and blocks.

On an MCU the blocks live in a raw partition on a microSD card; see [sdcard.md](sdcard.md) to prepare one.

### Binary data: `putbin.py`

`putbin.py file block [blockfile]` writes a binary file, such as a font
from `mkfont.py`, as one stream starting at byte 32 of `block`: the first
32 bytes of that block are zeros, kept for an HMAC of the data, and the
rest of the last block is zeros too. It prints the blocks it used and the
next free one, where the next file can go:

```
py ..\scripts\putbin.py cjk24.fnt 100 ..\bin\lfblocks.fb4
cjk24.fnt: 107974 bytes at byte 32 of block 100: blocks 100-126, next free block 127
```

Like `putblocks.py`, it needs a signed block file (or partition) and never
writes block 0. `getblocks.py` shows each binary block as one
`( BLOCK IGNORE ) block n: binary, crc32 ...` line, which `putblocks.py`
skips, so a dump written back leaves the binary blocks alone. `xmodem`
only takes text, so to get binary data onto the board, write it into the
block file on the PC and copy that to the card ([sdcard.md](sdcard.md)).

### Sending source over the terminal: `xmodem`

`xmodem` receives a source file over the terminal connection and writes it
into blocks, the way `putblocks.py` does on the PC: the same
`( BLOCK n )`, `( BLOCK++ )` and `( BLOCK IGNORE )` markers, each line a
128-column row, text before the first marker ignored. It works with any
terminal program that can send a file with XMODEM (the original protocol,
8-bit checksum; 1K packets are accepted too).

With RealTerm on the board's USB serial port:

1. Type `xmodem` and Enter. LiteForth says
   `xmodem: send the file now (XMODEM, checksum)` and waits up to a minute.
2. On RealTerm's *Send* tab, choose the file (`forth/core.f`, say), set the
   protocol to XMODEM with checksum, and send it.
3. When the transfer ends, LiteForth lists the blocks it wrote, for example
   `xmodem: 16 blocks written: 7 8 9 ...`. Then `1 load` or `7 load` as
   usual.

Each block is filled in a block buffer and written (`update save-buffers`)
when the next marker or the end of the file arrives. `putblocks.py` checks
the whole file before writing anything; `xmodem` writes as it goes. At the
first problem, such as a line longer than 128 bytes, more than 32 lines in
a block, block 0, or a block past the end of the storage, it stops writing
but receives the rest of the transfer, then reports the line, for example
`line 4, block 21: line is longer than 128 bytes; nothing written from there on`,
and the word fails with -124. Blocks before that point stay written. A
transfer that fails or that the sender cancels gives -123.

In UTF-8 text, you will never see a 0xFF. The interpreter replaces 0xFF with `' '`.

0xFF is interpreted as a space when listing screens.
Control characters (0x00 to 0x1F) and UTF-8 "bad bytes" (0xC0, 0xC1, and 0xF5 to 0xFE) are displayed as `?`.

LiteForth blocks contain UTF-8. Block editors should account for UTF-8.

---

## Forth Block Wordset

The **Block wordset** in Forth (ANS Forth / Forth-2012) provides an interface for managing persistent, fixed-size blocks of
memory—traditionally 1,024 bytes (1 KB) each—stored on mass storage (such as raw flash, disk sectors, or EEPROM) without requiring a full file system.

LiteForth deviates from this standard by making the size of each block 4 KB instead of 1 KB. 
4 KB blocks better-support the 4 KB sectors of SPI NOR Flash and are easier on SD cards.
It uses cell addressing, so an `a-addr` is just `addr`.

### Buffer & I/O Management

- `BLOCK` `( u -- addr )`  
  Returns the RAM memory address of the buffer containing block `u`. If the block is not currently loaded in RAM, the system reads it from storage into a buffer.

- `BUFFER` `( u -- addr )`  
  Assigns a RAM buffer to block `u` without reading its data from mass storage. Used when overwriting an entire block from scratch.

- `UPDATE` `( -- )`  
  Marks the currently active block buffer as "dirty" (modified). The system will automatically write dirty buffers back to persistent storage upon buffer reuse or explicit flushing.

- `SAVE-BUFFERS` `( -- )`  
  Writes all modified (`UPDATE`d) block buffers out to storage.

- `FLUSH` `( -- )`  
  Executes `SAVE-BUFFERS` and marks all block buffers as unassigned/empty.

- `EMPTY-BUFFERS` `( -- )`  
  Unassigns all block buffers without saving modified contents to storage (discards unsaved changes).

### Interpretation & Parsing

- `LOAD` `( u -- )`  
  Saves the current input source specification, sets the input source to block `u` (setting `BLK` to `u`), interprets the block's contents as Forth source code, and then restores the previous input source.

- `THRU` `( u1 u2 -- )`  
  Loads a contiguous range of blocks sequentially from block `u1` through `u2` inclusive.

- `BLK` `( -- addr )`  
  A system variable holding the number of the block currently being interpreted. Contains `0` when interpreting directly from the text input stream/terminal.

### Optional & Extension Words

- `SCR` `( -- addr )`  
  A system variable holding the block number most recently listed by `LIST`.

- `LIST` `( u -- )`  
  Displays block `u` formatted on screen as 16 lines of 64 characters each.
  
## LiteForth implementation

Block buffers are memory inside the sandbox - specifically, the RAM page.
LiteForth allocates them using BLOCK_SIZE_CELLS and SYSTEM_BLOCKS in `options.h`.
The `dirty` list is maintained in C.

Platform-specific access to physical blocks is in `blocks.c`.
