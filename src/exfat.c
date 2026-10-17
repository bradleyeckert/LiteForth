/*
 * A minimal exFAT reader (see exfat.h, and doc/exfat.md for the layout).
 * Microsoft's exFAT specification is the reference; offsets below are
 * from its tables. All fields are little-endian.
 */
#include <string.h>
#include "exfat.h"
#include "errcodes.h"

/* Boot sector fields */
#define BS_NAME         3       /* "EXFAT   " */
#define BS_ZERO         11      /* 53 bytes of zero */
#define BS_ZERO_LEN     53
#define BS_VOL_LENGTH   72      /* 8 bytes */
#define BS_FAT_OFFSET   80
#define BS_FAT_LENGTH   84
#define BS_HEAP_OFFSET  88
#define BS_CLUSTERS     92
#define BS_ROOT         96
#define BS_FLAGS        106     /* bit 1 VolumeDirty */
#define BS_BPS_SHIFT    108
#define BS_SPC_SHIFT    109
#define BS_FATS         110

/* MBR */
#define MBR_TABLE       446
#define MBR_ENTRY_SIZE  16
#define MBR_TYPE        4
#define MBR_START       8
#define MBR_SECTORS     12
#define PART_TYPE_EXFAT 0x07    /* also NTFS: the boot sector tells them apart */

/* Directory entries */
#define ENTRY_BYTES     32
#define ENTRIES_PER_SECTOR (EXFAT_SECTOR_BYTES / ENTRY_BYTES)
#define T_END           0x00
#define T_BITMAP        0x81
#define T_FILE          0x85
#define T_STREAM        0xC0
#define T_NAME          0xC1
#define T_INUSE         0x80
#define T_SECONDARY     0x40
#define NAME_CHARS      15      /* UTF-16 characters per name entry */
#define ATTR_DIRECTORY  0x10
#define FLAG_NOFATCHAIN 0x02
#define FAT_END         0xFFFFFFFFu
#define MAX_DIR_BYTES   (2u << 20)     /* 65536 entries: a corrupt FAT can't */
                                       /* keep a walk going for long */

static uint32_t dir_buf[EXFAT_SECTOR_BYTES / 4];   /* 4-byte aligned for DMA */
static uint32_t fat_buf[EXFAT_SECTOR_BYTES / 4];
static uint32_t fat_cached;     /* card sector in fat_buf, 0 = none */

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

uint16_t exfat_sum16(uint16_t sum, const uint8_t *p, uint32_t n) {
    while (n--) {
        sum = (uint16_t)(((sum & 1) ? 0x8000 : 0) + (sum >> 1) + *p++);
    }
    return sum;
}

uint32_t exfat_cluster_sector(const exfat_vol *vol, uint32_t c) {
    if (c < 2 || c - 2 >= vol->clusters) {
        return 0;
    }
    return vol->heap + ((c - 2) << vol->spc_shift);
}

/* The FAT entry of cluster c: the next cluster, FAT_END, or another value
   (free, bad, out of range) that ends the chain as broken. */
static int fat_next(exfat_read_fn read, const exfat_vol *vol, uint32_t c, uint32_t *next) {
    uint32_t sector = vol->fat + c / (EXFAT_SECTOR_BYTES / 4);
    if (sector != fat_cached) {
        int err = read(sector, (uint8_t *)fat_buf);
        if (err) {
            fat_cached = 0;
            return err;
        }
        fat_cached = sector;
    }
    *next = le32((const uint8_t *)&fat_buf[c % (EXFAT_SECTOR_BYTES / 4)]);
    return 0;
}

/* A position in a directory that follows the FAT */
typedef struct {
    exfat_read_fn read;
    const exfat_vol *vol;
    uint32_t cluster;
    uint32_t sector;            /* within the cluster */
    uint32_t index;             /* entry within the sector */
    uint32_t loaded;            /* card sector in dir_buf, 0 = none */
    uint32_t visited;           /* clusters so far, against FAT loops */
    int end;                    /* ran off the end of the chain */
} dir_cursor;

static void dir_open(dir_cursor *d, exfat_read_fn read, const exfat_vol *vol, uint32_t cluster) {
    d->read = read;
    d->vol = vol;
    d->cluster = cluster;
    d->sector = 0;
    d->index = 0;
    d->loaded = 0;
    d->visited = 1;
    d->end = 0;
}

/* The current entry, or NULL with *err set (0 at the end of the chain) */
static const uint8_t *dir_entry(dir_cursor *d, int *err, uint32_t *at_sector) {
    *err = 0;
    if (d->end) {
        return NULL;
    }
    uint32_t sector = exfat_cluster_sector(d->vol, d->cluster);
    if (sector == 0) {
        *err = ERR_BLK_PARSE_FAIL;
        return NULL;
    }
    sector += d->sector;
    if (sector != d->loaded) {
        *err = d->read(sector, (uint8_t *)dir_buf);
        if (*err) {
            d->loaded = 0;
            return NULL;
        }
        d->loaded = sector;
    }
    if (at_sector) {
        *at_sector = sector;
    }
    return (const uint8_t *)dir_buf + d->index * ENTRY_BYTES;
}

/* Moves to the next entry; 0 or an error */
static int dir_advance(dir_cursor *d) {
    if (++d->index < ENTRIES_PER_SECTOR) {
        return 0;
    }
    d->index = 0;
    if (++d->sector < (1u << d->vol->spc_shift)) {
        return 0;
    }
    d->sector = 0;
    uint32_t next;
    int err = fat_next(d->read, d->vol, d->cluster, &next);
    if (err) {
        return err;
    }
    if (next == FAT_END) {
        d->end = 1;
        return 0;
    }
    uint32_t most = MAX_DIR_BYTES >> (9 + d->vol->spc_shift);   /* at least 1 */
    if (next < 2 || next - 2 >= d->vol->clusters || ++d->visited > most) {
        return ERR_BLK_PARSE_FAIL;          /* free, bad, out of range, or a loop */
    }
    d->cluster = next;
    return 0;
}

static int is_exfat_boot(const uint8_t *b) {
    if (memcmp(b + BS_NAME, "EXFAT   ", 8) != 0 || b[510] != 0x55 || b[511] != 0xAA) {
        return 0;
    }
    for (int i = 0; i < BS_ZERO_LEN; i++) {
        if (b[BS_ZERO + i]) {
            return 0;
        }
    }
    return 1;
}

/* Checks the boot sector in b, for a volume at `start` that may be no
   longer than `limit` sectors (its partition; 0 = no limit), and fills in
   vol; 0 or an error */
static int take_boot(const uint8_t *b, uint32_t start, uint32_t limit, exfat_vol *vol) {
    uint64_t length = le64(b + BS_VOL_LENGTH);
    uint32_t fat = le32(b + BS_FAT_OFFSET);
    uint32_t fat_length = le32(b + BS_FAT_LENGTH);
    uint32_t heap = le32(b + BS_HEAP_OFFSET);
    uint32_t clusters = le32(b + BS_CLUSTERS);
    uint8_t spc = b[BS_SPC_SHIFT];
    if (limit && length > limit) {
        return ERR_BLK_PARSE_FAIL;          /* larger than its partition */
    }
    if (b[BS_BPS_SHIFT] != 9 || spc > 16 || b[BS_FATS] != 1 || clusters == 0
        || length > 0xFFFFFFFFu || (uint64_t)start + length > 0xFFFFFFFFu
        || fat < 24 || (uint64_t)fat + fat_length > heap || heap >= length
        || (uint64_t)fat_length * (EXFAT_SECTOR_BYTES / 4) < (uint64_t)clusters + 2
        || ((uint64_t)clusters << spc) > length - heap) {
        return ERR_BLK_PARSE_FAIL;
    }
    vol->start = start;
    vol->sectors = (uint32_t)length;
    vol->fat = start + fat;
    vol->heap = start + heap;
    vol->clusters = clusters;
    vol->root = le32(b + BS_ROOT);
    vol->spc_shift = spc;
    vol->dirty = (b[BS_FLAGS] >> 1) & 1;
    vol->bitmap = 0;
    vol->bitmap_bytes = 0;
    if (exfat_cluster_sector(vol, vol->root) == 0) {
        return ERR_BLK_PARSE_FAIL;
    }
    return 0;
}

/* Tries the volume whose boot sector is at `start`: the boot sector, then
   the allocation bitmap in the root directory; 0 or an error */
static int try_volume(exfat_read_fn read, uint32_t start, uint32_t limit, exfat_vol *vol) {
    const uint8_t *b = (const uint8_t *)dir_buf;
    int err = read(start, (uint8_t *)dir_buf);
    if (err) {
        return err;
    }
    if (!is_exfat_boot(b)) {
        return ERR_BLK_PARSE_FAIL;
    }
    err = take_boot(b, start, limit, vol);
    if (err) {
        return err;
    }
    /* The allocation bitmap: the root directory's first 81 entry */
    dir_cursor d;
    dir_open(&d, read, vol, vol->root);
    for (;;) {
        const uint8_t *e = dir_entry(&d, &err, NULL);
        if (e == NULL) {
            return err ? err : ERR_BLK_PARSE_FAIL;
        }
        if (e[0] == T_END) {
            return ERR_BLK_PARSE_FAIL;
        }
        if (e[0] == T_BITMAP && (e[1] & 1) == 0) {
            uint64_t bytes = le64(e + 24);
            vol->bitmap = le32(e + 20);
            vol->bitmap_bytes = (bytes > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)bytes;
            if (exfat_cluster_sector(vol, vol->bitmap) == 0 || bytes < (vol->clusters + 7) / 8) {
                return ERR_BLK_PARSE_FAIL;
            }
            return 0;
        }
        err = dir_advance(&d);
        if (err) {
            return err;
        }
    }
}

int exfat_mount(exfat_read_fn read, exfat_vol *vol) {
    const uint8_t *b = (const uint8_t *)dir_buf;
    fat_cached = 0;
    int err = read(0, (uint8_t *)dir_buf);
    if (err) {
        return err;
    }
    if (is_exfat_boot(b)) {
        return try_volume(read, 0, 0, vol);     /* no partition table */
    }
    if (b[510] != 0x55 || b[511] != 0xAA) {
        return ERR_BLK_PARSE_FAIL;
    }
    uint32_t starts[4], sizes[4];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = b + MBR_TABLE + i * MBR_ENTRY_SIZE;
        if (e[MBR_TYPE] == PART_TYPE_EXFAT && le32(e + MBR_START) != 0) {
            starts[n] = le32(e + MBR_START);
            sizes[n++] = le32(e + MBR_SECTORS);
        }
    }
    err = ERR_BLK_PARSE_FAIL;
    for (int i = 0; i < n; i++) {               /* the first one that works */
        err = try_volume(read, starts[i], sizes[i], vol);
        if (err == 0) {
            return 0;
        }
    }
    return err;
}

static uint16_t ascii_upper(uint16_t c) {
    return (c >= 'a' && c <= 'z') ? (uint16_t)(c - 'a' + 'A') : c;
}

/* Fills in the extent of file f: contiguity and the first sector */
static int file_extent(exfat_read_fn read, const exfat_vol *vol, uint8_t stream_flags, exfat_file *f) {
    uint32_t cluster_bytes_shift = 9u + vol->spc_shift;
    if (f->valid > f->length) {
        return ERR_BLK_PARSE_FAIL;
    }
    if (f->first_cluster == 0) {            /* nothing allocated */
        f->flags |= EXFAT_CONTIGUOUS;
        return (f->length == 0) ? 0 : ERR_BLK_PARSE_FAIL;
    }
    uint64_t n = (f->length + (1ull << cluster_bytes_shift) - 1) >> cluster_bytes_shift;
    if (n == 0 || f->first_cluster < 2 || f->first_cluster - 2 >= vol->clusters
        || n > vol->clusters - (f->first_cluster - 2)) {
        return ERR_BLK_PARSE_FAIL;
    }
    f->first_sector = exfat_cluster_sector(vol, f->first_cluster);
    if (stream_flags & FLAG_NOFATCHAIN) {
        f->flags |= EXFAT_NOFATCHAIN | EXFAT_CONTIGUOUS;
        return 0;
    }
    uint32_t c = f->first_cluster;
    for (uint64_t i = 1; i <= n; i++) {
        uint32_t next;
        int err = fat_next(read, vol, c, &next);
        if (err) {
            return err;
        }
        if (i == n) {
            if (next == FAT_END) {
                f->flags |= EXFAT_CONTIGUOUS;
            }
            return 0;
        }
        if (next != c + 1) {
            return 0;                       /* fragmented */
        }
        c = next;
    }
    return 0;
}

int exfat_find(exfat_read_fn read, const exfat_vol *vol, uint32_t dir,
               const char *name, exfat_file *file) {
    uint32_t len = (uint32_t)strlen(name);
    if (len == 0 || len > 255) {
        return ERR_FILE_NOT_FOUND;
    }
    uint16_t want_hash = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t ch[2] = { (uint8_t)ascii_upper((uint8_t)name[i]), 0 };
        want_hash = exfat_sum16(want_hash, ch, 2);
    }
    fat_cached = 0;

    dir_cursor d;
    dir_open(&d, read, vol, dir);
    int err;
    const uint8_t *e = dir_entry(&d, &err, NULL);
    while (e != NULL) {
        if (e[0] == T_END) {
            return ERR_FILE_NOT_FOUND;
        }
        if (e[0] != T_FILE || e[1] < 2 || e[1] > 18) {
            err = dir_advance(&d);
            if (err) {
                return err;
            }
            e = dir_entry(&d, &err, NULL);
            continue;
        }
        /* An entry set: 85, C0, C1..., maybe other secondaries */
        exfat_file f;
        memset(&f, 0, sizeof f);
        int count = e[1];
        uint16_t stored = le16(e + 2);
        uint16_t attributes = le16(e + 4);
        uint16_t sum = exfat_sum16(0, e, 2);
        sum = exfat_sum16(sum, e + 4, ENTRY_BYTES - 4);
        f.entry_sector = d.loaded;
        f.entry_offset = (uint16_t)(d.index * ENTRY_BYTES);
        uint8_t stream_flags = 0, name_len = 0;
        uint16_t hash = 0;
        uint32_t matched = 0, chars = 0;
        int ok = 1, taken = 1;
        for (int k = 1; k <= count; k++) {
            err = dir_advance(&d);
            if (err) {
                return err;
            }
            e = dir_entry(&d, &err, NULL);
            if (e == NULL) {
                return err ? err : ERR_BLK_PARSE_FAIL;
            }
            if ((e[0] & (T_INUSE | T_SECONDARY)) != (T_INUSE | T_SECONDARY)
                || (k == 1 && e[0] != T_STREAM)) {
                ok = 0;                     /* not part of the set: look at it afresh */
                taken = 0;
                break;
            }
            sum = exfat_sum16(sum, e, ENTRY_BYTES);
            if (k == 1) {
                stream_flags = e[1];
                name_len = e[3];
                hash = le16(e + 4);
                f.valid = le64(e + 8);
                f.first_cluster = le32(e + 20);
                f.length = le64(e + 24);
            } else if (e[0] == T_NAME) {
                for (int j = 0; j < NAME_CHARS && chars < name_len; j++, chars++) {
                    uint16_t c = le16(e + 2 + 2 * j);
                    if (chars < len && ascii_upper(c) == ascii_upper((uint8_t)name[chars])) {
                        matched++;
                    }
                }
            }
        }
        if (ok && sum == stored && name_len == len && hash == want_hash && matched == len) {
            if (attributes & ATTR_DIRECTORY) {
                f.flags |= EXFAT_DIRECTORY;
            }
            err = file_extent(read, vol, stream_flags, &f);
            if (err) {
                return err;
            }
            *file = f;
            return 0;
        }
        if (taken) {
            err = dir_advance(&d);
            if (err) {
                return err;
            }
        }
        e = dir_entry(&d, &err, NULL);
    }
    return err ? err : ERR_FILE_NOT_FOUND;
}
