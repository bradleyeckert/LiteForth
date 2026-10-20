#ifndef EXFAT_H
#define EXFAT_H

#include <stdint.h>

/*
 * A minimal exFAT reader: finds the volume on a card and a file in a
 * directory, so the block space can live in a file (lfblocks.fb4) that a
 * PC copied to the card. Nothing here writes. Portable C: the target
 * supplies a function that reads one 512-byte sector. doc/exfat.md has
 * the plan and the on-disk layout.
 */

#define EXFAT_SECTOR_BYTES  512

/* Reads card sector `sector` (512 bytes) into buf, which is 4-byte
   aligned. Returns 0 or a negative error code. */
typedef int (*exfat_read_fn)(uint32_t sector, uint8_t *buf);

typedef struct {                /* a mounted exFAT volume */
    uint32_t start;             /* first sector of the volume on the card */
    uint32_t sectors;           /* VolumeLength (32 bits: cards up to 2 TB) */
    uint32_t fat;               /* first sector of the FAT, on the card */
    uint32_t heap;              /* first sector of cluster 2, on the card */
    uint32_t clusters;          /* ClusterCount */
    uint32_t root;              /* first cluster of the root directory */
    uint32_t bitmap;            /* first cluster of the allocation bitmap */
    uint32_t bitmap_bytes;      /* its DataLength */
    uint8_t  spc_shift;         /* SectorsPerClusterShift */
    uint8_t  dirty;             /* VolumeDirty was set */
} exfat_vol;

#define EXFAT_NOFATCHAIN    1   /* exfat_file.flags: the stream says contiguous */
#define EXFAT_CONTIGUOUS    2   /* the clusters are contiguous (checked) */
#define EXFAT_DIRECTORY     4   /* it's a directory */

typedef struct {                /* a file found in a directory */
    uint64_t length;            /* DataLength, bytes */
    uint64_t valid;             /* ValidDataLength, bytes */
    uint32_t first_cluster;     /* 0 for an empty file */
    uint32_t first_sector;      /* card sector of first_cluster (0 if none) */
    uint32_t entry_sector;      /* card sector holding its 85 entry */
    uint16_t entry_offset;      /* byte offset of that entry in the sector */
    uint8_t  flags;             /* EXFAT_NOFATCHAIN | EXFAT_CONTIGUOUS | ... */
} exfat_file;

/**
 * Finds and checks the exFAT volume on a card: the first MBR partition of
 * type 07 that holds one, or a volume starting in sector 0 (a card
 * formatted without a partition table). Checks the boot sector's name,
 * must-be-zero bytes, signature, 512-byte sectors, cluster size and FAT
 * count, that the FAT fits before the cluster heap and the volume inside
 * its partition (the boot checksum isn't checked), and finds the
 * allocation bitmap in the root directory. A partition that fails is
 * skipped for the next one.
 * Returns 0 and fills in vol, ERR_BLK_PARSE_FAIL if there is no usable
 * exFAT volume, or the read function's error.
 */
int exfat_mount(exfat_read_fn read, exfat_vol *vol);

/**
 * Looks for `name` (ASCII, compared case-insensitively) in the directory
 * starting at cluster `dir`, following the FAT: vol->root for the root
 * directory, or a subdirectory found earlier that isn't NoFatChain
 * (contiguous subdirectories aren't handled yet). Deleted entries are
 * skipped; an entry set whose SetChecksum or NameHash doesn't match is
 * ignored. For a file without
 * NoFatChain, follows its FAT chain to see whether it's contiguous anyway.
 * Returns 0 and fills in file, ERR_FILE_NOT_FOUND, ERR_BLK_PARSE_FAIL for
 * a broken directory or FAT, or the read function's error.
 */
int exfat_find(exfat_read_fn read, const exfat_vol *vol, uint32_t dir,
               const char *name, exfat_file *file);

/* exfat_list's callback: name (ASCII, any other character as '?') and
   what the entry set says (length, valid, first_cluster, flags with only
   EXFAT_NOFATCHAIN and EXFAT_DIRECTORY; the clusters aren't followed).
   Returns 0 to go on, nonzero to stop the listing. */
typedef int (*exfat_list_fn)(void *ctx, const char *name, const exfat_file *file);

/**
 * Lists the directory starting at cluster `dir` (vol->root for the root
 * directory), as exfat_find walks it: deleted entries are skipped, and an
 * entry set whose SetChecksum doesn't match is left out. Calls fn for each
 * file and directory, in the order they're stored.
 * Returns 0, fn's nonzero result if it stopped the listing,
 * ERR_BLK_PARSE_FAIL for a broken directory or FAT, or the read
 * function's error.
 */
int exfat_list(exfat_read_fn read, const exfat_vol *vol, uint32_t dir,
               exfat_list_fn fn, void *ctx);

/**
 * The card sector where cluster c starts, or 0 if c isn't a cluster of
 * the volume.
 */
uint32_t exfat_cluster_sector(const exfat_vol *vol, uint32_t c);

/**
 * exFAT's 16-bit rotate-and-add over n bytes, starting from sum: used for
 * NameHash and SetChecksum (and by Forth code that writes entries).
 */
uint16_t exfat_sum16(uint16_t sum, const uint8_t *p, uint32_t n);

#endif /* EXFAT_H */
