# Preparing a microSD card for LiteForth

LiteForth keeps its blocks in an ordinary file, `lfblocks.fb4`, in the root
directory of the card's exFAT volume. Preparing a card is: format it as
exFAT (if it isn't already), copy the file, eject.

## 1. Format the card as exFAT

Cards over 32 GB (SDXC) come formatted as exFAT, so they're ready. Cards of
32 GB or less (SDHC) usually come as FAT32, which LiteForth doesn't read;
format those as exFAT. Formatting erases the card.

- **Windows:** in File Explorer, right-click the card, *Format...*, File
  system *exFAT*, *Quick Format*, *Start*.
- **macOS:** Disk Utility, select the card (the device, not just the
  volume), *Erase*, Format *ExFAT*, Scheme *Master Boot Record*.
- **Linux:** `sudo mkfs.exfat -L LITEFORTH /dev/sdX1` (from `exfatprogs`),
  on the card's first partition.

What LiteForth needs from the card:

- An MBR partition table with the exFAT volume in a partition of type `07`
  (what all three formatters make), or no partition table, with the volume
  starting at sector 0. GPT isn't read.
- 512-byte sectors and one FAT: every SD card formatter makes those. Any
  cluster size works.

## 2. Copy lfblocks.fb4 to the root directory

Drag `bin/lfblocks.fb4` (made by `forth/build.bat`) onto the card, into the
top folder, not a subfolder. The name may be in any case.

- **Its size sets the capacity.** The board gets the file's length / 4096
  blocks. The board writes blocks inside the file but never makes it
  longer, so give it room on the PC: `putblocks.py` and `putbin.py` extend
  it as they write, and blank blocks can be added the same way.
- **Block 0 must start with `LITEFORTH`.** Every block file the scripts and
  `lf` make does. Without it the board can read block 0 but writes nothing.
- **It must be in one piece.** LiteForth maps block n straight to the
  file's n-th 4 KB, so the file's clusters must be contiguous. A file copied
  onto a card with free space almost always is. If the board says it is
  fragmented, delete it and copy it again, or reformat the card first.

Eject the card before taking it out, so the PC finishes writing.

## 3. On the board

At startup the debug UART (USART1, on the WCHLinkE's COM port) says what
LiteForth found, for example:

```
V5F blocks: lfblocks.fb4, 4096 blocks at sector 34816 (exFAT)
```

and `capacity .` at the terminal shows the same number of blocks. If it's
0, the debug line says why:

| Message | What to do |
|---|---|
| `no SD card` | Check that the card is in |
| `no exFAT volume` | Format the card as exFAT (step 1) |
| `no lfblocks.fb4 in the exFAT root directory` | Copy the file to the top folder (step 2) |
| `lfblocks.fb4 is fragmented: copy it to the card again` | Delete it and copy it again |
| `lfblocks.fb4 is shorter than one block` | The file is empty or damaged |

If the board resets while reading blocks (the USB port drops and comes
back, often at the same block every time), suspect the card's power draw
before the software: a new SanDisk Ultra 256 GB SDXC (A1) card did exactly that on the
nanoCH32H417, and an older card worked. The startup log's
`V5F crumbs: reset flags` line then shows `power`. Try another card, a
powered USB hub, or a better supply to the slot.
| `...: block 0 doesn't start with LITEFORTH, read-only` | The file isn't a LiteForth block file |

The card is read once at startup, so after changing cards, restart (Ctrl+X
three times does).

Don't edit the file on the PC and on the board at the same time: take the
card out of one before using it in the other.

## Working with the card on the PC

The card's `lfblocks.fb4` is an ordinary block file, so everything that
works on `bin/lfblocks.fb4` works on it:

- `lf -k F:\lfblocks.fb4` runs the desktop LiteForth on the card's blocks.
- `py scripts\putblocks.py forth\core.f F:\lfblocks.fb4` writes source into
  it, and `getblocks.py` dumps it.

The desktop `lf -k` also takes an image of a whole card (an MBR and an
exFAT volume holding `lfblocks.fb4`) and finds the file inside it as the
board does. `scripts/mkexfat.py` makes small test images:

```
python3 scripts/mkexfat.py card.img --file lfblocks.fb4=bin/lfblocks.fb4
./bin/lf -k card.img
```

[exfat.md](exfat.md) describes how LiteForth reads exFAT and what comes
next (sector access for Forth, and files written by the board).
