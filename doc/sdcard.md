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
partition or mount it. If LiteForth doesn't find the `LITEFORTHBLK` header
in block 0 of the partition, it writes one, sized to the partition. It
doesn't clear the other blocks: on a used card they hold whatever was there
before. To start with blank blocks, see [Filling the blocks with spaces](#filling-the-blocks-with-spaces).

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

## Filling the blocks with spaces

Optional. Fresh blocks hold whatever the card held before, so `list` shows
noise and `load` would interpret it. To start clean, fill the partition with
spaces (ASCII 20h) and write the block 0 header yourself. On Linux, with
`P` and the sizes from the steps above:

```sh
tr '\000' ' ' < /dev/zero | head -c $(( BLOCKS_MIB * 1048576 )) | sudo dd of=${P}2 bs=1M status=progress
printf 'LITEFORTHBLK 1 1000 %X %X\n' $(( P2_SIZE / 8 )) $(( P2_SIZE / 8 )) | sudo dd of=${P}2 conv=notrunc
sync
```

On macOS, with `DISK` and the sizes from the macOS steps, write to the
partition's block device (`/dev/diskNs2`, not the raw `/dev/rdiskNs2`, which
only takes whole sectors):

```sh
tr '\000' ' ' < /dev/zero | head -c $(( BLOCKS_MIB * 1048576 )) | sudo dd of=/dev/${DISK}s2 bs=1m
printf 'LITEFORTHBLK 1 1000 %X %X\n' $(( P2_SIZE / 8 )) $(( P2_SIZE / 8 )) | sudo dd of=/dev/${DISK}s2 conv=notrunc
sync
``` On Windows there's no
built-in tool for writing a raw partition; let LiteForth write the header,
and overwrite blocks as you use them.

The header's last two numbers are, in hex, the number of blocks and the
first write-protected block (here: none, since it equals the number of
blocks). See [Block 0](popthehood.md#block-0) for the format.
