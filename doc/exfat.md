# exFAT on the microSD card

Status: **Part 1 is done** (finding `lfblocks.fb4`, and the C sector
functions under Part 2); the Forth words of Part 2 and the Forth file
writing of Part 3 are still a plan. [Phases](#phases) has the details.

## Goal

No partitioning, no raw disk access, no administrator:

- **Drag and drop.** `lfblocks.fb4` (10 to 20 MB, fonts included) is copied
  to the root directory of the card's ordinary exFAT volume, the one every
  SDXC card comes formatted with. LiteForth finds it at startup and uses it
  as its block space: block *n* is bytes *n* × 4096 to *n* × 4096 + 4095 of
  the file.
- **Other root files.** The same lookup finds other files in the root
  directory, such as a future update file, so that Forth can read them.
- **Streaming data, later, in Forth.** An app streams data to the card in
  16 KB pieces, into a file pre-expanded to a big field (say 4 GB), and
  trims the file to its real size when it closes it. The PC then sees an
  ordinary file.

The C side stays minimal: enough to find a file in the root directory and
read and write inside it. Everything that changes the file system (making
files, pre-expanding, trimming, subdirectories) is Forth, on top of raw
sector access.

## Who does what

| Part | Where | Does |
|---|---|---|
| Find the volume | C, `src/exfat.c` (shared by both targets) | MBR, boot sector checks, cluster geometry |
| Find a root file | C, `src/exfat.c` | Walk the root directory, match a name, return the file's extent |
| Block space | C, `src/card.c` and each target's `blocks.c` | Map block *n* onto `lfblocks.fb4` |
| Raw sectors | C, each target's `blocks.c` (done); Forth words (to do) | Read 512-byte sectors anywhere on the card, write them inside the exFAT volume, unwaited, with the DMA engine |
| Root-file lookup for Forth | C word | `( c-addr u -- ... )` gives a file's first sector, length and flags |
| Create, pre-expand, stream, trim | Forth | Allocation bitmap, directory entry sets, checksums, timestamps |
| Subdirectories | Forth | The same directory code, starting from another cluster |

## exFAT in brief

Only what this plan needs. All multi-byte fields are little-endian.
References at the end.

### Where the volume is

An SD card has an MBR in sector 0, with the exFAT volume in a partition of
type `07`. (A card can also be formatted without a partition table, with
the volume starting in sector 0, a "superfloppy". Some tools do that.) The
volume's first 12 sectors are the main boot region; the next 12 are a
backup copy.

Main boot sector (volume sector 0):

| Offset | Size | Field | Use here |
|---|---|---|---|
| 3 | 8 | FileSystemName | Must be `"EXFAT   "` |
| 11 | 53 | MustBeZero | Must be all zero: tells exFAT from FAT32/NTFS |
| 64 | 8 | PartitionOffset | Informational |
| 72 | 8 | VolumeLength | In sectors |
| 80 | 4 | FatOffset | Sectors from the volume's start to the FAT |
| 84 | 4 | FatLength | In sectors |
| 88 | 4 | ClusterHeapOffset | Sectors from the volume's start to cluster 2 |
| 92 | 4 | ClusterCount | |
| 96 | 4 | FirstClusterOfRootDirectory | |
| 106 | 2 | VolumeFlags | Bit 0 ActiveFat, bit 1 VolumeDirty, bit 2 MediaFailure |
| 108 | 1 | BytesPerSectorShift | 9 to 12; must be 9 (512 bytes) for us |
| 109 | 1 | SectorsPerClusterShift | Up to 25 − BytesPerSectorShift |
| 110 | 1 | NumberOfFats | 1 (2 only with TexFAT, which we don't support) |
| 510 | 2 | BootSignature | `55 AA` |

Sector 11 of the boot region holds a 32-bit checksum of sectors 0 to 10,
repeated. It skips bytes 106, 107 (VolumeFlags) and 112 (PercentInUse),
so setting VolumeDirty doesn't change it: for each byte,
`sum = (sum >> 1 | sum << 31) + byte`.

### Clusters, FAT and bitmap

- Cluster *c* (from 2) starts at sector
  `volume_start + ClusterHeapOffset + (c − 2) << SectorsPerClusterShift`.
  SDXC cards from the factory typically have 128 KB clusters, and so does
  Windows' default format for volumes of 32 GB and up.
- The FAT has a 32-bit entry per cluster: the next cluster of the chain,
  `FFFFFFFF` at the end, `FFFFFFF7` for a bad cluster. Entry *c* is at
  byte offset 4*c* from the FAT's start.
- **A file can skip the FAT.** If its stream entry has **NoFatChain** set,
  its clusters are contiguous and their FAT entries mean nothing. The
  root directory always uses the FAT.
- Which clusters are in use is recorded in the **allocation bitmap**, a
  file of its own (found from a directory entry of type `81` in the root
  directory). Bit *c* − 2 is cluster *c*. The FAT doesn't say whether a
  cluster is free; only the bitmap does.

### Directory entries

A directory is a run of 32-byte entries. Byte 0 is the type: bit 7 is
InUse, so `85` is a file and `05` a deleted one; `00` ends the directory.
The root directory holds a few special entries first:

| Type | Entry | Notes |
|---|---|---|
| `81` | Allocation bitmap | FirstCluster at 20, DataLength at 24 |
| `82` | Up-case table | Needed only to compare non-ASCII names |
| `83` | Volume label | |
| `85` | File | Starts an entry set: this, one `C0`, then 1 to 17 `C1` |
| `C0` | Stream extension | Where the data is |
| `C1` | File name | 15 UTF-16 characters each |

File entry (`85`): SecondaryCount at 1 (entries that follow), SetChecksum
at 2, FileAttributes at 4 (bit 4 Directory), and the create, modified and
accessed timestamps at 8, 12 and 16.

Stream extension (`C0`):

| Offset | Size | Field |
|---|---|---|
| 1 | 1 | GeneralSecondaryFlags: bit 0 AllocationPossible, bit 1 NoFatChain |
| 3 | 1 | NameLength, in UTF-16 characters (up to 255) |
| 4 | 2 | NameHash |
| 8 | 8 | ValidDataLength |
| 20 | 4 | FirstCluster |
| 24 | 8 | DataLength |

**ValidDataLength is what makes the 4 GB field cheap.** DataLength is the
space allocated to the file; ValidDataLength is how much of it has been
written. Reading past ValidDataLength gives zeros, so a file can be given
4 GB of clusters without writing 4 GB of zeros first.

**Checksums and hashes** use the same rotate-and-add as the boot checksum,
in 16 bits: `sum = (sum >> 1 | sum << 15) + byte`.

- **NameHash:** over the up-cased name, low byte then high byte of each
  UTF-16 character.
- **SetChecksum:** over every byte of the entry set, skipping bytes 2 and 3
  of the file entry (the checksum itself).

Every change to an entry set (a new ValidDataLength, say) means
recomputing SetChecksum.

**Timestamps:** 32 bits: year − 1980 in bits 25-31, month 21-24, day
16-20, hour 11-15, minute 5-10 and seconds / 2 in 0-4. A UtcOffset byte per timestamp says which zone it's in: `80` means
valid, UTC + 0. LiteForth's RTC keeps UTC, so it writes `80`.

## Part 1: C, finding a file

### `src/exfat.c` interface

Portable C with no target code. It reads sectors through a callback
supplied by the target. As built (see `src/exfat.h` for the exact
definitions):

```c
typedef int (*exfat_read_fn)(uint32_t sector, uint8_t *buf512);

typedef struct {                /* a mounted exFAT volume */
    uint32_t start;             /* first sector of the volume on the card */
    uint32_t sectors;           /* VolumeLength (cards up to 2 TB: 32 bits) */
    uint32_t fat;               /* first FAT sector, absolute */
    uint32_t heap;              /* first sector of cluster 2, absolute */
    uint32_t clusters;          /* ClusterCount */
    uint32_t root;              /* first cluster of the root directory */
    uint32_t bitmap;            /* first cluster of the allocation bitmap */
    uint32_t bitmap_bytes;      /* its DataLength */
    uint8_t  spc_shift;         /* SectorsPerClusterShift */
    uint8_t  dirty;             /* VolumeDirty was set */
} exfat_vol;

typedef struct {                /* a file found in a directory */
    uint32_t first_sector;      /* absolute sector of its first cluster */
    uint64_t length;            /* DataLength */
    uint64_t valid;             /* ValidDataLength */
    uint32_t first_cluster;
    uint32_t entry_sector;      /* where its entry set starts, for Forth */
    uint16_t entry_offset;      /* byte offset of the 85 entry in that sector */
    uint8_t  flags;             /* NoFatChain, contiguous, directory */
} exfat_file;

int exfat_mount(exfat_read_fn read, exfat_vol *vol);
int exfat_find(exfat_read_fn read, const exfat_vol *vol, uint32_t dir_cluster,
               const char *name, exfat_file *file);
```

`exfat_mount` reads sector 0, takes the first MBR partition of type `07`
(or, if sector 0 is itself an exFAT boot sector, the whole card), and
checks the boot sector: the name, MustBeZero, the signature, 512-byte
sectors, the shifts in range and one FAT. It also notes where the
allocation bitmap is (the root directory's `81` entry), for Forth. It doesn't verify the boot
checksum (11 sectors read for little gain); that could be a later option.

`exfat_find` walks the directory's cluster chain from `dir_cluster`
(the root, or a subdirectory from an earlier find), one sector at a time:

1. Skip entries whose InUse bit is clear; stop at `00`.
2. At a `85`, read its `C0` and `C1` entries (an entry set can cross into
   the next sector, and the next cluster).
3. Compare NameLength and NameHash with the target name first, then the
   characters. ASCII letters compare case-insensitively (the up-case
   table maps them in the obvious way); any other character must match
   exactly, so the up-case table is never read.
4. On a match, fill in `exfat_file`. If NoFatChain is clear, follow the
   file's FAT chain once to see whether it is contiguous anyway (20 MB in
   128 KB clusters is 160 FAT entries, one or two sectors).

Names are ASCII in C (`"lfblocks.fb4"`), widened to UTF-16 for the
comparison. An entry set whose SetChecksum or NameHash doesn't match is
skipped, as is a set broken off by an entry that isn't part of it. Two
static 512-byte buffers (directory and FAT): 2.7 KB of RV32 code and 1 KB
of RAM.

A corrupt card can't hang or mislead it: the boot sector must fit its
partition and keep the FAT before the cluster heap; a directory walk stops
after 65,536 entries (2 MB, against a FAT that loops); a file's clusters
must lie inside the volume, and ValidDataLength can't exceed DataLength.
VolumeDirty is reported but not acted on.

### The block space on `lfblocks.fb4`

`blk_init` (both targets) calls `card_find_blocks` (`src/card.c`), which
mounts the volume and looks for **`lfblocks.fb4` in the root
directory**. Block *n* is sector `first_sector + 8n`. The capacity is
ValidDataLength / 4096, rounded down, so the block words never read past
the written data. Block 0 must start with `LITEFORTH` to be writable;
without it the capacity is 1, read-only. No volume or no file: capacity
0.

USART1 says which it found, for example
`V5F blocks: lfblocks.fb4, 4096 blocks at sector 34816 (exFAT)`.

- **Contiguous files only.** `exfat_find` reports whether the file is
  contiguous, and `blk_init` only uses one that is. A file copied onto a
  card with plenty of free space comes out contiguous. If it isn't,
  `blk_init` says `lfblocks.fb4 is fragmented: copy it to the card again`
  and offers no blocks. An extent table (say 8 runs) could lift this
  later.
- **Writes stay inside the file**, at sector granularity, so they change
  no file system metadata and are safe: `update save-buffers`, the block
  editor and `write-block` work as before. The file's modified time
  doesn't change, which is harmless.
- **Eject on the PC first.** The card mustn't be in the PC while the board
  writes, nor the other way round. That's nothing new.

On the CH32H417, everything underneath is a run of 1 to 64 card sectors
through the DMA engine; a block is 8 of them. `blk_init` reads the boot
sector, the root directory and the FAT through the same engine, one
sector at a time (`blk_sector_read`).

On the desktop, `lf -k card.img` does the same with an image of a whole
card (an unsigned file holding an MBR or an exFAT boot sector), and a
plain signed block file works as before. The Windows raw-disk access
(`-k F:`) is gone: `lf -k F:\lfblocks.fb4` opens the card's file
through Windows instead.

## Part 2: sector access for the Forth file system

The C side is done, in both targets' `blocks.h`:

```c
int blk_sector_read_imm(uint32_t lba, uint32_t *dest, int count);   /* 1..64 sectors */
int blk_sector_write_imm(uint32_t lba, uint32_t *src, int count);
int blk_sector_read(uint32_t lba, uint32_t *dest, int count);       /* waits */
int blk_write_fence(uint32_t lba, int count);   /* 0 if inside the exFAT volume */
```

Reads reach the whole card; writes outside the exFAT volume give -96.
They share `blk_status` with the block functions.

Still to do: four Forth words on top of them:

| Word | Stack | Does |
|---|---|---|
| `read-sectors` | `( a lba n -- )` | Starts reading `n` 512-byte sectors (1 to 64) from absolute sector `lba` into the `n` × 128 cells at `a`; `block-status` says when it's done |
| `write-sectors` | `( a lba n -- )` | The same, writing |
| `volume` | `( -- a )` | Address of a small table: start, sectors, FAT, heap, clusters, root cluster, cluster shift, and the bitmap's first cluster and length |
| `root-file` | `( c-addr u -- a \| 0 )` | `exfat_find` on the root: 0 if not found, else the address of an `exfat_file` record |

- The sector words share the DMA engine, `block-status` and its rules
  (one transfer at a time, -125 while busy). A block transfer is the same
  thing with 8 sectors per block.
- **Writes are fenced to the exFAT volume.** `write-sectors` refuses any
  sector outside `start .. start + sectors − 1` (-96), so it can't touch
  the MBR or anything else on the card. Inside the
  volume, keeping the file system consistent is the Forth code's job.
- On the desktop the same words work on a card image. On a plain block
  file, its own sectors can be read and none written (there is no
  volume around it).

`exfat_vol` and `exfat_file` records live in C (a static `exfat_vol` and
one `exfat_file` slot for `root-file`); Forth reads their fields with
`@`. Their layout goes in the glossary.

## Part 3: Forth, writing files

Later, after Parts 1 and 2 run on the board. A sketch, to check the C
interface is enough.

### Making a file and pre-expanding it

1. **Find space.** Scan the allocation bitmap for a run of free clusters
   big enough for the field: 4 GB in 128 KB clusters is 32,768 clusters,
   4 KB of bitmap. The whole bitmap of a 64 GB card is 64 KB, 16 blocks.
2. **Mark the volume dirty** (VolumeFlags bit 1 in the boot sector; it
   isn't in the checksum, so only that sector is rewritten).
3. **Allocate.** Set the run's bits in the bitmap.
4. **Write the entry set.** Find three free consecutive entries in the
   directory (`85`, `C0`, one `C1` for a name of up to 15 characters):
   - Stream entry: DataLength 4 GB, ValidDataLength 0, FirstCluster,
     AllocationPossible and NoFatChain set (the run is contiguous, so the
     FAT isn't touched).
   - File entry: the timestamps from `rtc@`, UtcOffset `80`.
   - NameHash, then SetChecksum.
   A directory with no room left needs another cluster for the directory,
   which does use the FAT; the first version can just fail with a clear
   message.
5. **Clear VolumeDirty.**

### Streaming

- Data goes out in 16 KB pieces (4 blocks, 32 sectors) with
  `write-sectors`, double-buffered as in [blocks.md](blocks.md).
- Every so often (each MB, or each few seconds; open question), rewrite
  the stream entry with the new ValidDataLength and its SetChecksum. That's
  one sector, and it bounds what a power cut can lose.
- VolumeDirty stays clear while streaming, so a card pulled mid-stream
  doesn't prompt Windows to scan it: the file is consistent at every
  ValidDataLength update, just still 4 GB long.

### Closing (trimming)

1. Set VolumeDirty.
2. DataLength = ValidDataLength = the bytes written.
3. Clear the bits of the clusters past the last one used.
4. Rewrite the entry set with its new SetChecksum; set the modified
   timestamp.
5. Clear VolumeDirty.

A file left at 4 GB by a power cut can be recognized at the next start (a
name pattern, or a flag in a custom-defined bit of
GeneralSecondaryFlags; open question) and trimmed then, from its
ValidDataLength.

## Testing

- **Card images on the desktop.** `lf -k card.img` takes an image file
  with an MBR, an exFAT partition and `lfblocks.fb4` in its root, and maps
  the file as on the board. The C code is the same.
- **Test images (done).** `scripts/mkexfat.py` builds small test images:
  MBR or none, boot regions with checksum, FAT, bitmap, up-case table,
  root directory, and files: contiguous or fragmented, with or without
  NoFatChain, short ValidDataLength, deleted copies, padding to move a set
  across sector and cluster ends, a spoiled checksum. Its checksum and
  hash code follows the exFAT reference implementation's.
- **Images made by real formatters (to do).** One formatted and filled by
  Windows (format a spare card or a small partition, copy `lfblocks.fb4`,
  then save the first few MB with HxD or `dd`), and one from Linux
  `mkfs.exfat` + `fsck.exfat`. The container here can't install
  exfatprogs, so these come from the PC. They would catch anything
  `mkexfat.py` and `exfat.c` both get wrong the same way.
- **`unit_tests/exfat` (done, Unity):** mount (with and without an MBR,
  dirty, and rejecting NTFS, a non-zero MustBeZero, 4 KB sectors, a wrong
  partition type and no MBR), find (names in any case, not found, a long
  name after a deleted copy, a set crossing a sector, a fragmented root
  crossing clusters mid-set, fragmented and FAT-chained files,
  ValidDataLength, a directory, an empty file, a bad checksum),
  `card_find_blocks` (signed, unsigned, fragmented, partial last block,
  too big for the card, no volume), and the desktop `blocks.c` on a card
  image: block reads and writes, sector reads, the write fence. `run_lf.sh`
  then runs `lf -k` on an image holding a block file built from `core.f`,
  compares it with the same file used directly, and compiles `core.f`
  from it.
- **Part 3** is checked by the PC: an image the Forth code wrote must pass
  `chkdsk /f` on Windows and `fsck.exfat -n`, and show the right size and
  contents.
- **On the board:** copy `lfblocks.fb4` to the card's exFAT partition,
  restart, `capacity .`, `1 load`, then the block read and write tests
  already used for DMA.

## Phases

1. **This plan.** Done.
2. **`src/exfat.c`, `src/card.c`, `unit_tests/exfat`,
   `scripts/mkexfat.py`.** Done.
3. **`blk_init` on both targets**, exFAT only, and the sector functions.
   Desktop card images. `lf_exfat.c` and `lf_card.c` wrappers for the V5F.
   Docs: `sdcard.md`, `quickstart.md`, the CH32 README. Done; the V5F code
   is compile-checked with clang for RV32.
4. **On the board** with an exFAT card. Done: `1 load` and every block
   of `lfblocks.fb4` read on the board (a new 256 GB card browned the
   board out on some reads; an older card works).
5. **Sector words and `root-file` for Forth.** Regression tests on a card
   image.
6. **Forth file writing** (Part 3), in its own block range of `core.f` or
   a separate source file, with the PC checks.

Phases 2 to 4 are enough for drag and drop.

## Open questions

Settled: a fragmented `lfblocks.fb4` is refused with a message.

1. **The "run of blocks"** in your note: is that another `.fb4`-style file
   mapped into the block space at some offset (say a second file starting
   at block 100000), or a file Forth reads with `read-sectors`? The plan
   covers the second; the first is a small addition to `blk_init`.
2. **The update file:** what will it hold (a new flash image, new
   firmware for a bootloader), and does C need to find it at startup, or
   only Forth?
3. **How often to update ValidDataLength** while streaming, and how a
   file left open by a power cut is recognized.
4. **Sector words' units:** 512-byte sectors (plan), or 4 KB blocks at any
   sector? Sectors suit FAT, bitmap and directory updates; blocks suit
   streaming. The 1-to-64 sector range covers both.
5. **Write fence:** is "inside the exFAT volume" enough, or should
   `write-sectors` need an explicit unlock word first?

## References

- Microsoft, *exFAT file system specification* (published 2019):
  https://learn.microsoft.com/windows/win32/fileio/exfat-specification
- Directory entry layouts, summarized:
  https://www.active-undelete.com/xfat_entries.htm
- SD Association, *SD Specifications Part 2: File System Specification*
  (formatting rules for SDXC cards: cluster sizes and alignment).
- The Linux kernel's exFAT driver, `fs/exfat/` (`exfat_raw.h` has the
  on-disk structures).
