# Preparing a microSD card for LiteForth

LiteForth keeps its blocks in a raw partition on the card, with no file
system. The card gets two partitions:

| # | Holds | MBR type | Where |
|---|---|---|---|
| 1 | A FAT32 or exFAT file system, for files you share with a PC | `0C` (FAT32, LBA) or `07` (exFAT) | From 1 MiB to the start of partition 2 |
| 2 | LiteForth blocks, 4 KB each | `DA` (non-FS data) | The last 64 MiB of the card (16,384 blocks), or whatever size you choose |

The partition table is the classic MBR ("DOS") kind that SD cards come
with, not GPT. LiteForth reads it from sector 0 and uses the first entry of
type `DA`:

- **Capacity:** the entry's sector count / 8 (a 4 KB block is eight 512-byte
  sectors). 64 MiB gives 16,384 blocks.
- **Alignment:** the partition must start on a 64 KB boundary, a multiple of
  128 sectors. The steps below put both partitions on 1 MiB boundaries
  (2048 sectors), which are always 64 KB aligned, and make the block
  partition a whole number of MiB.

Type `DA` keeps Windows and macOS from offering to format the block
partition or mount it.

The partition must also start with the signature `LITEFORTH`, the first 9
bytes of block 0. It marks the card as LiteForth's: LiteForth never writes
it, so it can't mistake another card's raw data for blocks and overwrite
it. Without the signature, LiteForth reports a capacity of 1 block and
lets you read block 0, to see what's there, but refuses to write any block.
On the CH32H417, the debug UART (USART8) says which case it found at
startup. Partitioning doesn't write the signature, so it's the last step for every system:
[Marking the partition for LiteForth](#marking-the-partition-for-liteforth).
Block 0 will hold more metadata as LiteForth develops; for now the
signature is all it needs.

**Partitioning erases the whole card.** Each section below starts by
finding the card's device name. Check it twice: choosing the wrong disk
destroys that disk's contents.

Pick the size of the block partition before you start: 64 MiB is plenty
for source (the design notes in [popthehood.md](popthehood.md) estimate
8 MB for a large system). Any whole number of MiB works.

---

## Linux

You need `sfdisk` (in `util-linux`, already installed on most systems),
plus `mkfs.vfat` (`dosfstools`) for FAT32 or `mkfs.exfat` (`exfatprogs`)
for exFAT.

1. Find the card. Plug it in and run `lsblk`. It's the disk whose size
   matches the card, for example `sdb` (USB reader) or `mmcblk0` (built-in
   reader). Use the whole disk, not a partition such as `sdb1`.

2. Set the device and the block partition size, and unmount anything the
   desktop mounted from the card:

   ```sh
   DEV=/dev/sdb          # the card: check with lsblk!
   BLOCKS_MIB=64         # size of the LiteForth block partition
   sudo umount ${DEV}?* 2>/dev/null
   ```

3. Work out where the partitions go. Everything is in 512-byte sectors;
   2048 sectors is 1 MiB.

   ```sh
   SECTORS=$(sudo blockdev --getsz $DEV)              # sectors on the card
   P2_SIZE=$(( BLOCKS_MIB * 2048 ))                   # block partition
   P2_START=$(( (SECTORS - P2_SIZE) / 2048 * 2048 ))  # at the end, on a MiB boundary
   P1_START=2048                                      # FAT partition from 1 MiB
   P1_SIZE=$(( P2_START - P1_START ))                 # up to the block partition
   echo "FAT: $P1_START +$P1_SIZE  blocks: $P2_START +$P2_SIZE ($(( P2_SIZE / 8 )) blocks)"
   ```

4. Write the partition table. For exFAT, change `type=c` to `type=7`.

   ```sh
   sudo sfdisk $DEV <<EOF
   label: dos
   unit: sectors
   start=$P1_START, size=$P1_SIZE, type=c
   start=$P2_START, size=$P2_SIZE, type=da
   EOF
   ```

5. Format partition 1. Partition names get a `p` when the disk name ends in
   a digit: `/dev/sdb1`, but `/dev/mmcblk0p1`.

   ```sh
   P=$DEV; case $DEV in *[0-9]) P=${DEV}p ;; esac
   sudo mkfs.vfat -F 32 -n LITEFORTH ${P}1     # FAT32
   # or: sudo mkfs.exfat -L LITEFORTH ${P}1    # exFAT
   ```

6. Check: `sudo sfdisk -d $DEV` lists both partitions. Partition 2 should
   have `type=da` and a `start=` that divides evenly by 128.

---

## Windows

Use `diskpart` from an administrator Command Prompt or PowerShell. It needs
Windows 10 version 1703 or later to use more than one partition on a
removable card.

1. Start `diskpart` and find the card:

   ```
   diskpart
   list disk
   ```

   The card is the disk whose size matches it, for example `Disk 2   29 GB`.

2. Work out the size of partition 1 in MB: the card's size in MB minus
   twice the block partition size. For a card listed as `29 GB` and a 64 MB
   block partition: 29 × 1024 − 128 = 29568. Leaving a little more than
   the block partition needs makes sure it fits after rounding; any unused
   space at the end does no harm.

3. Partition and format. Replace `2` with the card's disk number and
   `29568` with your size. `clean` erases the card's partition table.

   ```
   select disk 2
   detail disk
   clean
   create partition primary size=29568 align=1024
   format fs=fat32 quick label=LITEFORTH
   assign
   create partition primary size=64 id=DA align=1024
   list partition
   exit
   ```

   Run `detail disk` first and make sure it describes the SD card, not
   another drive. `align=1024` puts each partition on a 1 MB boundary.
   `format fs=fat32` only works for partitions up to 32 GB; for a larger
   card use `fs=exfat`. The block partition gets no drive letter, and
   Windows leaves it alone.

4. Check: `list partition` (before `exit`) shows partition 1 and a 64 MB
   partition 2. Its offset is a whole number of MB, so it's 64 KB aligned.

---

## macOS

macOS's `diskutil` can't make a partition of type `DA`, so this uses
`fdisk`'s interactive editor for the partition table and `diskutil` for the
file system. These steps follow macOS's `fdisk` and `diskutil` documentation
but haven't been tried on a Mac; the check in step 6 shows whether the
result is right.

1. Find the card: `diskutil list`. It's an `(external, physical)` disk whose
   size matches, for example `/dev/disk4`. Then unmount it:

   ```sh
   DISK=disk4            # the card: check with diskutil list!
   diskutil unmountDisk /dev/$DISK
   ```

2. Work out where the partitions go. `diskutil info` gives the size in
   bytes:

   ```sh
   BLOCKS_MIB=64
   BYTES=$(diskutil info /dev/$DISK | sed -n 's/.*Disk Size:.*(\([0-9]*\) Bytes).*/\1/p')
   SECTORS=$(( BYTES / 512 ))
   P2_SIZE=$(( BLOCKS_MIB * 2048 ))
   P2_START=$(( (SECTORS - P2_SIZE) / 2048 * 2048 ))
   P1_START=2048
   P1_SIZE=$(( P2_START - P1_START ))
   echo "FAT: $P1_START +$P1_SIZE  blocks: $P2_START +$P2_SIZE"
   ```

3. Edit the partition table with `sudo fdisk -e /dev/r$DISK`. At its
   `fdisk:` prompt, enter the commands below. Each `edit` asks for the
   partition ID, whether to use CHS mode, and then the offset and size in
   sectors; answer with the numbers from step 2.

   ```
   erase
   edit 1        ID: 0C   CHS mode: n   offset: P1_START   size: P1_SIZE
   edit 2        ID: DA   CHS mode: n   offset: P2_START   size: P2_SIZE
   print
   write
   quit
   ```

   For exFAT, give partition 1 the ID `07` instead of `0C`. If macOS
   remounts or complains about the card afterwards, unmount it again.

4. Format partition 1 (`s1`):

   ```sh
   diskutil eraseVolume FAT32 LITEFORTH ${DISK}s1
   # or: diskutil eraseVolume ExFAT LITEFORTH ${DISK}s1
   ```

5. Eject the card with `diskutil eject /dev/$DISK`.

6. Check: `sudo fdisk /dev/$DISK` prints the table. Partition 2 should have
   ID `DA` and a start that divides evenly by 128.

---

## Marking the partition for LiteForth

Write `LITEFORTH` at the start of partition 2. Optionally, fill the
partition with spaces (ASCII 20h) first: otherwise the blocks hold
whatever the card held before, so `list` shows noise and `load` would
interpret it. Filling overwrites the start of the partition too, so do it
before writing the signature.

### Linux

With `P` and `BLOCKS_MIB` from the Linux steps:

```sh
# optional: fill the block partition with spaces
tr '\000' ' ' < /dev/zero | head -c $(( BLOCKS_MIB * 1048576 )) | sudo dd of=${P}2 bs=1M status=progress
# required: the signature
printf 'LITEFORTH' | sudo dd of=${P}2 conv=notrunc
sync
```

### macOS

With `DISK` and `BLOCKS_MIB` from the macOS steps. Write to the
partition's block device (`/dev/diskNs2`), not the raw `/dev/rdiskNs2`,
which only takes whole sectors:

```sh
# optional: fill the block partition with spaces
tr '\000' ' ' < /dev/zero | head -c $(( BLOCKS_MIB * 1048576 )) | sudo dd of=/dev/${DISK}s2 bs=1m
# required: the signature
printf 'LITEFORTH' | sudo dd of=/dev/${DISK}s2 conv=notrunc
sync
```

### Windows

Windows has no built-in tool for writing a raw partition, so use a disk
editor from the list below, running as administrator. In HxD: *Tools >
Open disk*, choose the card's physical disk with *Open as Readonly*
unchecked, *Search > Go to* byte offset `P2_START` × 512 (the start of
partition 2; see [Finding a block](#finding-a-block)), type `LITEFORTH`
over the first 9 bytes in the text column, and *File > Save*. In Active@
Disk Editor, open partition 2 itself and type it at offset 0.

### Checking it

On Linux, `sudo head -c 9 ${P}2` prints `LITEFORTH` (on macOS,
`sudo head -c 9 /dev/${DISK}s2`). On the board, `capacity .` shows the
partition's size in blocks (16384 for 64 MiB) instead of 1.

---

## Editing blocks with a raw disk editor

The block partition has no file system, so you look at and change its data
with a raw disk (sector) editor. Run it as administrator (Windows) or root
(Linux). The block partition is never mounted, so nothing else is using
it; still, keep a copy before changing anything, since an editor writes
straight to the card.

### Finding a block

Block *n* is the 4 KB (0x1000 bytes) at offset *n* × 4096 from the start
of partition 2. Block 0 starts with the `LITEFORTH` signature.

- An editor that opens **partition 2 itself** (`/dev/sdb2` on Linux, or
  the partition chosen in the editor's disk dialog) shows block *n* at
  offset *n* × 0x1000.
- An editor that opens **the whole card** needs the partition's start
  sector first: `P2_START` from the steps above, `sudo sfdisk -d /dev/sdb`
  on Linux, or `list partition` / `detail partition` in `diskpart`. Block
  *n* is then at byte offset `P2_START` × 512 + *n* × 4096, or at sector
  `P2_START` + 8 × *n*.

Block text is UTF-8, 4096 bytes per block. LiteForth shows a block as 32
lines of 128 columns (`SCREEN_COLUMNS`), so an editor set to 128 bytes per
row lines up with what `list` shows. Overwrite characters rather than
inserting or deleting them, so the block stays exactly 4096 bytes.

### Windows

- **[HxD](https://mh-nexus.de/en/hxd/)** (free). *Tools > Open disk*
  lists physical disks and lettered volumes. The block partition has no
  drive letter, so open the card's physical disk (with *Open as Readonly*
  unchecked to edit), then use *Search > Go to* with the byte offset
  above. *Edit > Select block* and *File > Save as* can copy a range of
  sectors to a file.
- **[Active@ Disk Editor](https://www.disk-editor.org/)** (free; Windows,
  Linux and macOS). Lists the partitions on each disk, so you can open
  partition 2 directly, and has templates for decoding the MBR.
- **[WinHex](https://www.x-ways.net/winhex/)** (commercial). *Tools > Open
  Disk* shows physical disks and their partitions, and it can interpret the
  MBR and jump to a partition.
- **[wxHexEditor](https://www.wxhexeditor.org/)** (free, open source;
  Windows, Linux and macOS). Opens disk devices for editing. Its last
  release is a 2017 beta.

### Linux

- **`hexedit`** (terminal, in most distributions' packages):
  `sudo hexedit /dev/sdb2` opens the block partition. Enter jumps to an
  offset (prefix hex with `0x`: `0x5000` is block 5), Tab switches between the
  hex and text columns, F2 saves and Ctrl+C quits without saving.
- **[wxHexEditor](https://www.wxhexeditor.org/)**: *Devices > Open Disk
  Device*, then choose `/dev/sdb2`. Start it with `sudo`.
- **[Active@ Disk Editor](https://www.disk-editor.org/)**: the Linux
  version opens the partition directly, as on Windows.
- **`dd`, for one block at a time.** Copy block *n* out, edit the copy with
  any hex or text editor that keeps it at 4096 bytes, and write it back:

  ```sh
  N=5
  sudo dd if=/dev/sdb2 of=block$N.bin bs=4096 skip=$N count=1        # read block N
  fold -w 128 block$N.bin                                             # view it as LiteForth shows it
  sudo dd if=block$N.bin of=/dev/sdb2 bs=4096 seek=$N count=1 conv=notrunc,fsync  # write it back
  ```

  Check the size before writing back: `stat -c %s block$N.bin` must say
  4096.
