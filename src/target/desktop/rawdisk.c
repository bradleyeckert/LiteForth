/*
 * Block storage on a raw disk partition (see rawdisk.h and doc/sdcard.md).
 *
 * The partition logic is the CH32H417's (src/target/CH32H417/V5F/User/
 * blocks.c): read the MBR from sector 0, take the first entry of type 0xDA,
 * check that it starts on a 64 KB boundary and that its block 0 starts with
 * BLK_SIGNATURE. Below it, a small device layer reads and writes bytes at
 * an offset: the Windows physical disk, or else a disk image file.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L         /* fseeko */
#define _FILE_OFFSET_BITS 64            /* 64-bit off_t on 32-bit hosts */
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "rawdisk.h"
#include "blocks.h"
#include "errcodes.h"

#define MBR_TABLE           446         /* offset of the 4 partition entries */
#define MBR_ENTRY_SIZE      16
#define MBR_TYPE            4           /* offsets within an entry */
#define MBR_START           8
#define MBR_SECTORS         12
#define PART_TYPE_BLOCKS    0xDA        /* "non-FS data" */
#define PART_ALIGN_BYTES    65536u      /* the partition starts on 64 KB */

static uint64_t part_offset;            /* byte offset of block 0 on the disk */

/* ------------------------------------------------------------------------
 * Device layer: dev_open, dev_read, dev_write, dev_close. Offsets and
 * lengths are whole sectors; a length is at most BLK_SIZE_BYTES.
 */
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>

static HANDLE disk = INVALID_HANDLE_VALUE;
static uint8_t *bounce;                 /* page aligned, for unbuffered I/O */

static void printWinError(const char *what) {
    char msg[256] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), 0, msg, sizeof(msg), NULL);
    fprintf(stderr, "blocks: %s: %s", what, msg);   /* msg ends with a newline */
}

int rawdisk_is_drive(const char *name) {
    return isalpha((unsigned char)name[0]) && name[1] == ':'
        && (name[2] == '\0' || ((name[2] == '\\' || name[2] == '/') && name[3] == '\0'));
}

/* Opens the physical disk that holds drive letter name[0]. */
static int dev_open(const char *name, uint32_t *sector_bytes) {
    char letter = (char)toupper((unsigned char)name[0]);
    char path[40];

    /* Only a removable disk: never a system or data drive by mistake */
    char root[4] = { letter, ':', '\\', '\0' };
    if (GetDriveTypeA(root) != DRIVE_REMOVABLE) {
        fprintf(stderr, "blocks: %c: is not a removable drive\n", letter);
        return -1;
    }

    /* The disk that holds the drive's volume */
    snprintf(path, sizeof(path), "\\\\.\\%c:", letter);
    HANDLE vol = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
    if (vol == INVALID_HANDLE_VALUE) {
        printWinError(path);
        return -1;
    }
    VOLUME_DISK_EXTENTS ext;
    DWORD got = 0;
    BOOL ok = DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                              NULL, 0, &ext, sizeof(ext), &got, NULL);
    CloseHandle(vol);
    if (!ok || ext.NumberOfDiskExtents < 1) {
        printWinError("can't find the disk that holds the drive");
        return -1;
    }
    unsigned long disknum = (unsigned long)ext.Extents[0].DiskNumber;

    /* The whole disk. Partition 1 stays mounted, so share it; the block
       partition has no volume, so Windows lets us write to it. */
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%lu", disknum);
    disk = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (disk == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            fprintf(stderr, "blocks: raw access to %s (drive %c:) needs an "
                    "administrator: run lf as administrator\n", path, letter);
        } else {
            printWinError(path);
        }
        return -1;
    }

    *sector_bytes = 512;
    DISK_GEOMETRY_EX geo;
    if (DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                        &geo, sizeof(geo), &got, NULL)) {
        *sector_bytes = geo.Geometry.BytesPerSector;
    }

    bounce = VirtualAlloc(NULL, BLK_SIZE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (bounce == NULL) {
        printWinError("can't allocate a sector buffer");
        CloseHandle(disk);
        disk = INVALID_HANDLE_VALUE;
        return -1;
    }
    fprintf(stderr, "blocks: drive %c: is on disk %lu\n", letter, disknum);
    return 0;
}

static int dev_seek(uint64_t offset) {
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)offset;
    return SetFilePointerEx(disk, pos, NULL, FILE_BEGIN) ? 0 : ERR_BLK_SEEK_FAIL;
}

static int dev_read(uint64_t offset, void *buf, uint32_t len) {
    DWORD got = 0;
    int ior = dev_seek(offset);
    if (ior) return ior;
    if (!ReadFile(disk, bounce, len, &got, NULL) || got != len) {
        return ERR_BLK_READ_FAIL;
    }
    memcpy(buf, bounce, len);
    return 0;
}

static int dev_write(uint64_t offset, const void *buf, uint32_t len) {
    DWORD put = 0;
    int ior = dev_seek(offset);
    if (ior) return ior;
    memcpy(bounce, buf, len);
    if (!WriteFile(disk, bounce, len, &put, NULL) || put != len) {
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}

static void dev_close(void) {
    if (disk != INVALID_HANDLE_VALUE) {
        CloseHandle(disk);
        disk = INVALID_HANDLE_VALUE;
    }
    if (bounce != NULL) {
        VirtualFree(bounce, 0, MEM_RELEASE);
        bounce = NULL;
    }
}

#else   /* not Windows: a disk image file, or a whole-disk device */

static FILE *disk = NULL;

int rawdisk_is_drive(const char *name) {
    (void)name;
    return 0;                           /* no drive letters here */
}

static int dev_open(const char *name, uint32_t *sector_bytes) {
    disk = fopen(name, "r+b");
    if (disk == NULL) {
        disk = fopen(name, "rb");       /* writes will fail */
    }
    if (disk == NULL) {
        fprintf(stderr, "blocks: can't open %s\n", name);
        return -1;
    }
    *sector_bytes = 512;
    return 0;
}

static int dev_read(uint64_t offset, void *buf, uint32_t len) {
    if (fseeko(disk, (off_t)offset, SEEK_SET) != 0) return ERR_BLK_SEEK_FAIL;
    if (fread(buf, 1, len, disk) != len) return ERR_BLK_READ_FAIL;
    return 0;
}

static int dev_write(uint64_t offset, const void *buf, uint32_t len) {
    if (fseeko(disk, (off_t)offset, SEEK_SET) != 0) return ERR_BLK_SEEK_FAIL;
    if (fwrite(buf, 1, len, disk) != len || fflush(disk) != 0) return ERR_BLK_WRITE_FAIL;
    return 0;
}

static void dev_close(void) {
    if (disk != NULL) {
        fclose(disk);
        disk = NULL;
    }
}

#endif

/* ------------------------------------------------------------------------
 * The block partition
 */

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int rawdisk_open(const char *name, uint32_t *blocks, int *writable) {
    static uint8_t sec[BLK_SIZE_BYTES];
    uint32_t ss = 0;                    /* bytes per sector */
    *blocks = 0;
    *writable = 0;
    part_offset = 0;
    rawdisk_close();

    if (dev_open(name, &ss)) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (ss < 512 || ss > BLK_SIZE_BYTES || (BLK_SIZE_BYTES % ss) != 0) {
        fprintf(stderr, "blocks: %lu-byte sectors aren't supported\n", (unsigned long)ss);
        rawdisk_close();
        return ERR_BLK_OPEN_FAIL;
    }

    /* The MBR: find the block partition */
    if (dev_read(0, sec, ss)) {
        fprintf(stderr, "blocks: can't read the disk's MBR\n");
        rawdisk_close();
        return ERR_BLK_OPEN_FAIL;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        fprintf(stderr, "blocks: the disk has no MBR partition table\n");
        rawdisk_close();
        return ERR_BLK_OPEN_FAIL;
    }
    uint32_t start = 0, sectors = 0;
    int found = 0;
    for (int i = 0; i < 4 && !found; i++) {
        const uint8_t *e = &sec[MBR_TABLE + i * MBR_ENTRY_SIZE];
        if (e[MBR_TYPE] == PART_TYPE_BLOCKS) {
            start = le32(&e[MBR_START]);
            sectors = le32(&e[MBR_SECTORS]);
            found = 1;
        }
    }
    uint64_t part_bytes = (uint64_t)sectors * ss;
    if (!found || part_bytes < BLK_SIZE_BYTES) {
        fprintf(stderr, "blocks: no LiteForth block partition (type DA) on the disk\n");
        rawdisk_close();
        return ERR_BLK_OPEN_FAIL;
    }
    part_offset = (uint64_t)start * ss;
    *blocks = 1;                        /* block 0, read-only, until proven */

    /* Block 0 must start with the signature, on a 64 KB boundary */
    if (part_offset % PART_ALIGN_BYTES) {
        fprintf(stderr, "blocks: the block partition at sector %lu isn't 64 KB "
                "aligned: read-only\n", (unsigned long)start);
    } else if (dev_read(part_offset, sec, ss)) {
        fprintf(stderr, "blocks: can't read block 0\n");
    } else if (memcmp(sec, BLK_SIGNATURE, BLK_SIGNATURE_LEN) != 0) {
        fprintf(stderr, "blocks: block 0 doesn't start with " BLK_SIGNATURE ": read-only\n");
    } else {
        uint64_t n = part_bytes / BLK_SIZE_BYTES;
        *blocks = (n > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)n;
        *writable = 1;
        fprintf(stderr, "blocks: %lu blocks at sector %lu\n",
                (unsigned long)*blocks, (unsigned long)start);
    }
    return 0;
}

int rawdisk_read(uint32_t blk, uint32_t *dest) {
    return dev_read(part_offset + (uint64_t)blk * BLK_SIZE_BYTES, dest, BLK_SIZE_BYTES);
}

int rawdisk_write(uint32_t blk, const uint32_t *src) {
    return dev_write(part_offset + (uint64_t)blk * BLK_SIZE_BYTES, src, BLK_SIZE_BYTES);
}

void rawdisk_close(void) {
    dev_close();
}
