/* Unit tests for rawdisk.c: the MBR and partition logic, on image files */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "rawdisk.h"
#include "blocks.h"
#include "errcodes.h"

#define IMG "test_rawdisk.img"
#define SECTOR 512

/* Writes a disk image: an MBR whose entry `slot` has type `type`, start
   sector `start` and `sectors` sectors (plus a FAT entry in the other
   slot 0 or 1), and the partition filled with '.', starting with `sig`
   (if not NULL). `boot_sig` 0 leaves out the 55AA. */
static void make_image(int slot, int type, unsigned start, unsigned sectors,
                       const char *sig, int boot_sig) {
    static unsigned char buf[SECTOR];
    FILE *f = fopen(IMG, "wb");
    assert(f);
    memset(buf, 0, sizeof(buf));
    unsigned char *e = &buf[446 + 16 * (slot ^ 1)];
    e[4] = 0x0C; e[8] = 1;              /* a FAT partition at sector 1 */
    e = &buf[446 + 16 * slot];
    e[4] = (unsigned char)type;
    for (int i = 0; i < 4; i++) {
        e[8 + i] = (unsigned char)(start >> (8 * i));
        e[12 + i] = (unsigned char)(sectors >> (8 * i));
    }
    if (boot_sig) { buf[510] = 0x55; buf[511] = 0xAA; }
    fwrite(buf, 1, SECTOR, f);
    memset(buf, 0, sizeof(buf));
    for (unsigned s = 1; s < start; s++) fwrite(buf, 1, SECTOR, f);
    memset(buf, '.', sizeof(buf));
    for (unsigned s = 0; s < sectors; s++) {
        if (s == 0 && sig) memcpy(buf, sig, strlen(sig));
        fwrite(buf, 1, SECTOR, f);
        if (s == 0) memset(buf, '.', sizeof(buf));
    }
    fclose(f);
}

static void test_good(void) {
    uint32_t blocks = 0; int writable = 0;
    static uint32_t in[BLOCK_SIZE_CELLS], out[BLOCK_SIZE_CELLS];
    make_image(1, 0xDA, 128, 4 * 8, BLK_SIGNATURE, 1);   /* 64 KB, 4 blocks */
    assert(rawdisk_open(IMG, &blocks, &writable) == 0);
    assert(blocks == 4 && writable == 1);

    for (int i = 0; i < BLOCK_SIZE_CELLS; i++) out[i] = 0x01020304u * (uint32_t)i;
    assert(rawdisk_write(2, out) == 0);
    assert(rawdisk_read(2, in) == 0);
    assert(memcmp(in, out, sizeof(in)) == 0);
    assert(rawdisk_read(0, in) == 0);
    assert(memcmp(in, BLK_SIGNATURE, BLK_SIGNATURE_LEN) == 0);
    rawdisk_close();

    /* block 2 is at byte 64 KB + 2 * 4 KB of the image */
    FILE *f = fopen(IMG, "rb");
    assert(f);
    assert(fseek(f, 65536 + 2 * 4096, SEEK_SET) == 0);
    assert(fread(in, 1, sizeof(in), f) == sizeof(in));
    fclose(f);
    assert(memcmp(in, out, sizeof(in)) == 0);
    printf("test_good passed!\n");
}

static void test_first_slot(void) {         /* type DA in the first entry */
    uint32_t blocks = 0; int writable = 0;
    make_image(0, 0xDA, 256, 2 * 8, BLK_SIGNATURE, 1);
    assert(rawdisk_open(IMG, &blocks, &writable) == 0);
    assert(blocks == 2 && writable == 1);
    rawdisk_close();
    printf("test_first_slot passed!\n");
}

static void test_read_only(void) {
    uint32_t blocks = 0; int writable = 0;
    make_image(1, 0xDA, 130, 4 * 8, BLK_SIGNATURE, 1);   /* not 64 KB aligned */
    assert(rawdisk_open(IMG, &blocks, &writable) == 0);
    assert(blocks == 1 && writable == 0);
    rawdisk_close();
    make_image(1, 0xDA, 128, 4 * 8, "SOMETHING", 1);     /* no signature */
    assert(rawdisk_open(IMG, &blocks, &writable) == 0);
    assert(blocks == 1 && writable == 0);
    rawdisk_close();
    printf("test_read_only passed!\n");
}

static void test_no_partition(void) {
    uint32_t blocks = 9; int writable = 9;
    make_image(1, 0x83, 128, 4 * 8, BLK_SIGNATURE, 1);   /* no type DA */
    assert(rawdisk_open(IMG, &blocks, &writable) == ERR_BLK_OPEN_FAIL);
    assert(blocks == 0 && writable == 0);
    make_image(1, 0xDA, 128, 4 * 8, BLK_SIGNATURE, 0);   /* no 55AA */
    assert(rawdisk_open(IMG, &blocks, &writable) == ERR_BLK_OPEN_FAIL);
    make_image(1, 0xDA, 128, 4, BLK_SIGNATURE, 1);       /* less than a block */
    assert(rawdisk_open(IMG, &blocks, &writable) == ERR_BLK_OPEN_FAIL);
    assert(rawdisk_open("no_such_disk.img", &blocks, &writable) == ERR_BLK_OPEN_FAIL);
    printf("test_no_partition passed!\n");
}

int main(void) {
    printf("Running unit tests for rawdisk (messages on stderr are expected)...\n");
#ifndef _WIN32
    assert(rawdisk_is_drive("F:") == 0);   /* drive letters are Windows-only */
#endif
    test_good();
    test_first_slot();
    test_read_only();
    test_no_partition();
    remove(IMG);
    printf("All rawdisk tests passed successfully!\n");
    return 0;
}
