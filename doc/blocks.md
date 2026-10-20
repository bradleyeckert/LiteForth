# Blocks in LiteForth

LiteForth uses blocks rather than files.

The desktop ecosystem is stream-based, with source code in text files.
Desktop Forth moved away from blocks. The MCU environment has no such ecosystem.

Charles H. Moore, the creator of Forth, has consistently advocated for raw blocks over operating system file systems throughout his career.
Chuck's Forths have always used modal editors, usable from the terminal, rather than cursor-based input.
VIM users can tell you there is a tradeoff between the speed of typing and the speed of cursor positioning.
A non-modal editor is not necessarily faster to use.

File and block abstractions are both very 20th-Century. They are blocking, which gives up the gains provided by DMA.
LiteForth adds non-blocking block I/O.
Code can kick off a read or write, then go off and do other things while waiting for the transfer to finish.

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

On an MCU the blocks live in `lfblocks.fb4` on an exFAT microSD card; see [sdcard.md](sdcard.md) to prepare one.

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

Like `putblocks.py`, it needs a signed block file and never
writes block 0. `getblocks.py` shows each binary block as one
`( BLOCK IGNORE ) block n: binary, crc32 ...` line, which `putblocks.py`
skips, so a dump written back leaves the binary blocks alone. On the
board, `100 ymodem` does the same with a file sent over the terminal (see
below).

### Sending files over the terminal: `ymodem`

`ymodem ( n -- )` receives files over the terminal connection by YMODEM
and writes them into blocks. YMODEM sends each file's name and length
first, so the board keeps exactly the file's bytes, and it can send
several files in one go (a batch).

- **`0 ymodem`: Forth source**, written the way `putblocks.py` does: the
  same `( BLOCK n )`, `( BLOCK++ )` and `( BLOCK IGNORE )` markers, each
  line a 128-column row, text before the first marker ignored. This is
  the quick way to load a few blocks during development.
- **`100 ymodem`: binary**, written the way `putbin.py` does: from byte 32
  of block 100 on, the first 32 bytes zeros and the rest of the last block
  zeros. The next file of a batch starts in the block after. A font goes
  onto the board this way.
- **`-1 ymodem`: a whole block file**, block 0 included, plain or sealed,
  replacing the one on the card, so the card needn't come out of the
  board (below).

From kwai, `~x core.f` types `0 ymodem` and sends the file, and
`~x cjk24.fnt 100` types `100 ymodem` and sends the font. With another
terminal program, type the word, then send the file(s) with YMODEM (in
Tera Term: *File > Transfer > YMODEM > Send*). LiteForth asks for the
first file for up to a minute, and reports each file when the transfer
ends, for example

```
ymodem: core.f: 16 blocks written: 7 8 9 ...
ymodem: cjk24.fnt: 107974 bytes from byte 32 of block 100 to block 126, next free block 127
```

Then `1 load` or `7 load` as usual.

For source, each block is filled in a block buffer and written
(`update save-buffers`) when the next marker or the end of the file
arrives. `putblocks.py` checks the whole file before writing anything;
`ymodem` writes as it goes. At the first problem, such as a line longer
than 128 bytes, more than 32 lines in a block, block 0, or a block past
the end of the storage, it stops writing that file but receives the rest,
then reports the line, for example
`line 4, block 21: line is longer than 128 bytes; nothing written from there on`,
and the word fails with -124. Blocks before that point stay written. A
binary file that wouldn't fit before the end of the storage isn't written
(-92). A transfer that fails or that the sender cancels gives -123.

On the CH32H417, the V5F turns off the Ctrl+X x3 escape while `ymodem`
runs, since a binary file may hold three 0x18 bytes in a row.

### Replacing the whole block file: `-1 ymodem`

From kwai, `~x lfblocks.fb4 -1` types `-1 ymodem` and sends the file. It
goes into the space the block file already has (on a card, the size of
`lfblocks.fb4`, which the board can't change), so it must be whole 4 KB
blocks and fit; blocks after it are left as they were. One file per batch.

1. Block 0 arrives first and is checked before anything is written. It must
   start with `LITEFORTH`; a sealed file must be one this board would mount
   ([encrypted-blocks.md](admin/encrypted-blocks.md): signature, product,
   revision, a recipient), so a file the board can't open never replaces
   one it can.
2. Block 0 on the card becomes a plain block 0 that says the install
   hasn't finished, and the other blocks are written as they arrive. The
   real block 0 goes in last. A transfer cut short leaves a plain block
   file whose block 0 says so: send the file again.
3. When the batch ends, the whole file is read back and compared with what
   came (BLAKE2s), the block file is mounted again, which sets `capacity`,
   and every content block of a sealed file is read to check its hash.

```
ymodem: lfblocks.fb4: installed, 122 blocks, read back and checked; capacity now 121
ymodem: sealed file, revision 2: all 120 content blocks pass their hashes
```

Then `1 load` as usual. The block buffers are emptied first, so unsaved
changes are lost. Refusals write nothing and give -124 (a bad block 0 or
size) or -92 (too big); a sealed file whose blocks fail their hashes is
already written, since its hash tables come last, and gives -33. On the
desktop, `lf` takes it into the block file named by `-k` (a card image
too), if that file starts with `LITEFORTH`.

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
  
## Transfers without waiting

`block`, `save-buffers` and the other words above wait for the storage.
Three more words start a transfer and return at once, so a program can do
other work while blocks move:

- `read-block` `( a u n -- )`  
  Starts reading `n` blocks (1 to 8), from block `u` on, into the
  `n` × 1024 cells at `a`.

- `write-block` `( a u n -- )`  
  Starts writing the `n` × 1024 cells at `a` to `n` blocks (1 to 8), from
  block `u` on.

- `block-status` `( -- n )`  
  How the last transfer is doing: 1 still reading, 2 still writing, 0 done
  (or none started), negative: the error it failed with. The result stays
  until the next transfer starts.

They bypass the block buffers: the data goes straight between the card and
the cells at `a`. Only one transfer runs at a time. The `n` blocks are
consecutive, block `u` at `a`, block `u`+1 at `a` + 1024, and so on; on
the CH32H417 they move as one multi-block transfer of up to 32 KB, which
is faster than `n` separate ones.

### How they run

On the CH32H417 the data moves by DMA while the VM keeps running. No
interrupt is involved: each call of `block-status` checks the transfer and,
when the data is through, takes the next step itself (stopping the card's
multi-block transfer, then, for a write, asking the card until it has
finished programming). So a program has to keep calling `block-status`
until it reads 0 or an error, but it can call it as late as it likes. On
the nanoCH32H417 with its microSD card a 4 KB read takes about 0.5 to
0.9 ms (writes took about 1.8 ms with the earlier driver, which
waited); a step that makes no progress for a
second fails with -126.

On the desktop the transfer is done before `read-block` or `write-block`
returns, and `block-status` reads 0 (or the error) at once. Code written
for the board runs there unchanged.

### Usage hints

**Wait politely.** A loop that calls `pause` lets other tasks run while
the transfer finishes:

```forth
: wait-block  ( -- n )  begin block-status dup 0< over 0= or 0= while drop pause repeat ;
```

It leaves 0 when the transfer went through, or the error code.

**Find the cells.** For a read, the `n` × 1024 cells at `a` must be
writable; for either, they must all be in one memory page (-9 otherwise).
The RAM page is small (4096 cells on the CH32H417, half of them the two
block buffers), so the handy source is a block buffer: `buffer` with a
block number you aren't using gives one without reading anything.

```forth
500 buffer dup 9 1 read-block   ( other work here )   wait-block .
```

That leaves the buffer's address on the stack, for working on the data.
The two block buffers sit side by side, so the lower of the two has room
for two blocks:

```forth
500 buffer 501 buffer min  dup 9 2 read-block   ( blocks 9 and 10 )   wait-block .
```

A write can come from anywhere readable, flash included: `0 3000 2
write-block` copies the first 2048 cells of flash page 0 to blocks 3000
and 3001. More than two blocks need a bigger writable area than the
standard memory map has, for a read, or flash, for a write.

The buffer is only borrowed. The next `block` or `buffer` for another
block may take it (with two buffers, the least recently used goes), and
so may another task that uses blocks; the terminal empties the buffers at
the end of each line, and the cells keep their contents, but the next
block word may reuse them. Use the cells within one word or one line, and
don't call block words in between. On the board, the demo's LCD clock
reads font blocks whenever the terminal is idle, so even the next line is
too late: a test that read into a buffer on one line and wrote it back on
the next wrote font data.

**Leave the cells alone until it's done.** During a read the cells fill
up as the card delivers; during a write the DMA is still reading them. Use
or change them only after `block-status` reads 0.

**One at a time.** Starting a transfer while one is running gives -125.
The block words (`block`, `save-buffers`, `flush`, `load`, ...) first wait
for a transfer you started, and then report only their own result. So if
you care how yours went, read `block-status` before using them.

**Keep the buffers in step.** A `read-block` doesn't see changes that are
in a buffer (`update`) but not yet saved: `save-buffers` first. After a
`write-block` of a block that is also in a buffer, `block` still returns
the buffer's old copy: `empty-buffers` after it, or write through the
buffer instead (`update save-buffers`).

**Double buffering.** To stream through a run of blocks, start reading
block k+1 into one area, work on block k in the other, then
`wait-block` and swap the two.

**Errors.**

| Code | Meaning |
|---|---|
| -9 | `a` isn't writable (for a read), or the `n` × 1024 cells cross a page, or `a` is in the I/O page |
| -24 | `n` isn't 1 to 8 |
| -92 | The blocks go past the end of the storage |
| -94 | The read failed (the card reported an error) |
| -95 | The write failed |
| -96 | The storage is write-protected (block 0 doesn't start with `LITEFORTH`) |
| -125 | A transfer is still running |
| -126 | A transfer made no progress for a second |

The start words give -9, -24, -92, -96 and -125 at once, and -94 or -95 if the
card refuses the command. Failures during the transfer show up in
`block-status`. On the CH32H417, USART1 also gets a
line (`V5F blk: read failed, STA ...`) with the SDIO status.

## LiteForth implementation

Block buffers are memory inside the sandbox - specifically, the RAM page.
LiteForth allocates them using BLOCK_SIZE_CELLS and SYSTEM_BLOCKS in `options.h`.
The `dirty` list is maintained in C.

Platform-specific access to physical blocks is in `blocks.c`.
