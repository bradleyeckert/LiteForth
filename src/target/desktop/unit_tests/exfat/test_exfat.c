/*
 * Tests src/exfat.c (mount, find), src/card.c (where the block space is)
 * and the card-image side of the desktop blocks.c, on images made by
 * scripts/mkexfat.py (see the makefile for what each one holds).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "exfat.h"
#include "card.h"
#include "blocks.h"
#include "errcodes.h"

static FILE *image;
static int reads;                       /* sectors read through image_read */

static int image_read(uint32_t sector, uint8_t *buf) {
    reads++;
    if (fseek(image, (long)sector * 512, SEEK_SET) != 0 || fread(buf, 1, 512, image) != 512) {
        return ERR_BLK_READ_FAIL;
    }
    return 0;
}

static void open_image(const char *name) {
    image = fopen(name, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(image, name);
    reads = 0;
}

void setUp(void) {
    image = NULL;
}

void tearDown(void) {
    if (image) {
        fclose(image);
    }
}

/* Copies an image to scratch.img and applies a patch: n bytes at offset */
static void patched(const char *from, long offset, const void *bytes, size_t n) {
    FILE *in = fopen(from, "rb");
    FILE *out = fopen("scratch.img", "wb");
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    static char chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, in)) > 0) {
        fwrite(chunk, 1, got, out);
    }
    fclose(in);
    fseek(out, offset, SEEK_SET);
    fwrite(bytes, 1, n, out);
    fclose(out);
}

/* ---------------------------------------------------------------- mount */

void test_mount_mbr_card(void) {
    exfat_vol v;
    open_image("a.img");
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &v));
    TEST_ASSERT_EQUAL_UINT32(2048, v.start);
    TEST_ASSERT_EQUAL_UINT32(16384, v.sectors);
    TEST_ASSERT_EQUAL_UINT32(2048 + 128, v.fat);
    TEST_ASSERT_EQUAL_UINT8(3, v.spc_shift);
    TEST_ASSERT_EQUAL_UINT32(2, v.bitmap);          /* mkexfat puts it first */
    TEST_ASSERT_EQUAL_UINT32((v.clusters + 7) / 8, v.bitmap_bytes);
    TEST_ASSERT_EQUAL_UINT8(0, v.dirty);
    TEST_ASSERT_EQUAL_UINT32(v.heap, exfat_cluster_sector(&v, 2));
    TEST_ASSERT_EQUAL_UINT32(v.heap + 8, exfat_cluster_sector(&v, 3));
    TEST_ASSERT_EQUAL_UINT32(0, exfat_cluster_sector(&v, 1));
    TEST_ASSERT_EQUAL_UINT32(0, exfat_cluster_sector(&v, v.clusters + 2));
}

void test_mount_without_mbr(void) {
    exfat_vol v;
    open_image("b.img");
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &v));
    TEST_ASSERT_EQUAL_UINT32(0, v.start);
    TEST_ASSERT_EQUAL_UINT8(0, v.spc_shift);
}

void test_mount_dirty(void) {
    exfat_vol v;
    open_image("d.img");
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &v));
    TEST_ASSERT_EQUAL_UINT8(6, v.spc_shift);
    TEST_ASSERT_EQUAL_UINT8(1, v.dirty);
}

void test_mount_rejects_others(void) {
    exfat_vol v;
    /* not exFAT: the name says NTFS */
    patched("a.img", 2048L * 512 + 3, "NTFS    ", 8);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    fclose(image);
    /* MustBeZero isn't */
    patched("a.img", 2048L * 512 + 40, "x", 1);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    fclose(image);
    /* 4 KB sectors */
    patched("a.img", 2048L * 512 + 108, "\x0c", 1);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    fclose(image);
    /* the partition isn't type 07 */
    patched("a.img", 446 + 4, "\x0b", 1);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    fclose(image);
    /* no MBR signature either */
    patched("a.img", 510, "\0\0", 2);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    remove("scratch.img");
}

/* ----------------------------------------------------------------- find */

static exfat_vol vol;

static int find(const char *name, exfat_file *f) {
    return exfat_find(image_read, &vol, vol.root, name, f);
}

static void mount(const char *name) {
    open_image(name);
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &vol));
}

/* The 32-bit value at byte offset off of a file found, through its
   first sector (the file must be contiguous) */
static uint32_t file_word(const exfat_file *f, uint32_t off) {
    uint8_t s[512];
    TEST_ASSERT_EQUAL_INT(0, image_read(f->first_sector + off / 512, s));
    uint32_t w;
    memcpy(&w, s + off % 512, 4);
    return w;
}

void test_find_any_case(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(0, find("lfblocks.fb4", &f));
    TEST_ASSERT_EQUAL_INT(0, find("LFBLOCKS.FB4", &f));
    TEST_ASSERT_EQUAL_INT(0, find("LfBlocks.Fb4", &f));
    TEST_ASSERT_EQUAL_UINT64(65536, f.length);
    TEST_ASSERT_EQUAL_UINT64(65536, f.valid);
    TEST_ASSERT_TRUE(f.flags & EXFAT_NOFATCHAIN);
    TEST_ASSERT_TRUE(f.flags & EXFAT_CONTIGUOUS);
    TEST_ASSERT_FALSE(f.flags & EXFAT_DIRECTORY);
    TEST_ASSERT_EQUAL_UINT32(exfat_cluster_sector(&vol, f.first_cluster), f.first_sector);
    TEST_ASSERT_EQUAL_UINT32(5, file_word(&f, 5 * 4096 + 100));     /* block 5 holds 5 */
}

void test_find_not_there(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(ERR_FILE_NOT_FOUND, find("lfblocks.fb", &f));
    TEST_ASSERT_EQUAL_INT(ERR_FILE_NOT_FOUND, find("lfblocks.fb44", &f));
    TEST_ASSERT_EQUAL_INT(ERR_FILE_NOT_FOUND, find("nothing", &f));
    TEST_ASSERT_EQUAL_INT(ERR_FILE_NOT_FOUND, find("", &f));
}

void test_find_long_name_after_deleted_copy(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(0, find("a long file name.dat", &f));
    TEST_ASSERT_EQUAL_UINT64(5000, f.length);
    TEST_ASSERT_EQUAL_UINT32(1, file_word(&f, 4096 + 8));     /* block 1 holds 1 */
}

void test_find_records_entry_position(void) {
    exfat_file f;
    uint8_t s[512];
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(0, find("notes.txt", &f));
    TEST_ASSERT_EQUAL_INT(0, image_read(f.entry_sector, s));
    TEST_ASSERT_EQUAL_HEX8(0x85, s[f.entry_offset]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, s[f.entry_offset + 32]);
    /* lfblocks.fb4's set, after 9 deleted entries, crosses a sector */
    TEST_ASSERT_EQUAL_INT(0, find("lfblocks.fb4", &f));
    TEST_ASSERT_EQUAL_INT(0, image_read(f.entry_sector, s));
    TEST_ASSERT_EQUAL_HEX8(0x85, s[f.entry_offset]);
    TEST_ASSERT_EQUAL_UINT16(480, f.entry_offset);  /* the last entry of the sector */
}

void test_find_fragmented_and_fat_chained(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(0, find("frag.bin", &f));
    TEST_ASSERT_FALSE(f.flags & EXFAT_NOFATCHAIN);
    TEST_ASSERT_FALSE(f.flags & EXFAT_CONTIGUOUS);
    TEST_ASSERT_EQUAL_INT(0, find("fatc.bin", &f));
    TEST_ASSERT_FALSE(f.flags & EXFAT_NOFATCHAIN);
    TEST_ASSERT_TRUE(f.flags & EXFAT_CONTIGUOUS);   /* contiguous anyway */
}

void test_find_valid_length_dir_empty(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(0, find("half.bin", &f));
    TEST_ASSERT_EQUAL_UINT64(65536, f.length);
    TEST_ASSERT_EQUAL_UINT64(30000, f.valid);
    TEST_ASSERT_EQUAL_INT(0, find("sub", &f));
    TEST_ASSERT_TRUE(f.flags & EXFAT_DIRECTORY);
    TEST_ASSERT_EQUAL_INT(0, find("empty.txt", &f));
    TEST_ASSERT_EQUAL_UINT64(0, f.length);
    TEST_ASSERT_EQUAL_UINT32(0, f.first_cluster);
    TEST_ASSERT_EQUAL_UINT32(0, f.first_sector);
}

void test_find_ignores_bad_checksum(void) {
    exfat_file f;
    mount("a.img");
    TEST_ASSERT_EQUAL_INT(ERR_FILE_NOT_FOUND, find("bad.txt", &f));
    TEST_ASSERT_EQUAL_INT(0, find("empty.txt", &f));    /* the set before it is fine */
}

void test_find_through_fragmented_root(void) {
    exfat_file f;
    /* 512-byte clusters: 16 entries each, and the root's clusters aren't
       neighbors, so the walk follows the FAT, crossing clusters mid-set */
    mount("b.img");
    TEST_ASSERT_EQUAL_INT(0, find("x.bin", &f));
    TEST_ASSERT_EQUAL_UINT64(8192, f.length);
    TEST_ASSERT_EQUAL_UINT32(1, file_word(&f, 4096 + 600));    /* block 1, 2nd sector */
    TEST_ASSERT_EQUAL_INT(0, find("lfblocks.fb4", &f));
    TEST_ASSERT_EQUAL_UINT64(32768, f.length);
}

void test_sum16(void) {
    /* NameHash of "A" (0x0041): rotate-and-add of 41 then 00 */
    uint8_t a[2] = { 0x41, 0x00 };
    uint16_t h = exfat_sum16(0, a, 2);
    TEST_ASSERT_EQUAL_HEX16((uint16_t)((((0x41 & 1) << 15) | (0x41 >> 1)) + 0), h);
}

/* ----------------------------------------------------------------- card */

static card_map map;
static char msg[120];

void test_card_signed_file(void) {
    open_image("a.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_EXFAT_FILE, map.kind);
    TEST_ASSERT_EQUAL_UINT32(16, map.blocks);
    TEST_ASSERT_EQUAL_INT(1, map.writable);
    TEST_ASSERT_EQUAL_UINT32(map.file.first_sector, map.base);
    TEST_ASSERT_NOT_NULL(strstr(msg, "16 blocks"));
}

void test_card_unsigned_file(void) {
    open_image("b.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_EXFAT_FILE, map.kind);
    TEST_ASSERT_EQUAL_UINT32(1, map.blocks);
    TEST_ASSERT_EQUAL_INT(0, map.writable);
    TEST_ASSERT_NOT_NULL(strstr(msg, "read-only"));
}

void test_card_fragmented_file(void) {
    open_image("c.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_NONE, map.kind);
    TEST_ASSERT_EQUAL_UINT32(0, map.blocks);
    TEST_ASSERT_EQUAL_INT(1, map.mounted);
    TEST_ASSERT_NOT_NULL(strstr(msg, "fragmented"));
}

void test_card_partial_block_and_bounds(void) {
    open_image("d.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_UINT32(24, map.blocks);       /* 100000 / 4096 */
    /* a card too small to hold it */
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, map.base + 8, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_NONE, map.kind);
    TEST_ASSERT_NOT_NULL(strstr(msg, "past the end"));
}

void test_card_nothing(void) {
    patched("a.img", 2048L * 512 + 3, "NTFS    ", 8);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_NONE, map.kind);
    TEST_ASSERT_EQUAL_INT(0, map.mounted);
    TEST_ASSERT_EQUAL_STRING("no exFAT volume", msg);
    remove("scratch.img");
}

/* ------------------------------------------------- corrupt structures */

/* Changes stream entry bytes of lfblocks.fb4 (entries 2-4 of the root's
   first sector) in scratch.img, a copy of f.img if fresh, and fixes the
   SetChecksum */
static void spoil_stream(int fresh, int offset, const void *bytes, int n) {
    exfat_vol v;
    if (fresh) {
        patched("f.img", 0, "", 0);
    }
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &v));
    uint32_t root = exfat_cluster_sector(&v, v.root);
    fclose(image);
    image = NULL;
    FILE *f = fopen("scratch.img", "r+b");
    uint8_t sec[512];
    fseek(f, (long)root * 512, SEEK_SET);
    TEST_ASSERT_EQUAL_size_t(512, fread(sec, 1, 512, f));
    uint8_t *set = sec + 64;
    TEST_ASSERT_EQUAL_HEX8(0x85, set[0]);
    memcpy(set + 32 + offset, bytes, n);
    uint16_t sum = exfat_sum16(0, set, 2);
    sum = exfat_sum16(sum, set + 4, 96 - 4);
    set[2] = (uint8_t)sum;
    set[3] = (uint8_t)(sum >> 8);
    fseek(f, (long)root * 512, SEEK_SET);
    fwrite(sec, 1, 512, f);
    fclose(f);
}

void test_first_cluster_past_the_volume(void) {
    exfat_file f;
    uint8_t c[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    spoil_stream(1, 20, c, 4);
    mount("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, find("lfblocks.fb4", &f));
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_NONE, map.kind);
    remove("scratch.img");
}

void test_valid_past_length_without_clusters(void) {
    exfat_file f;
    uint8_t zero[8] = { 0 };
    spoil_stream(1, 20, zero, 4);                   /* FirstCluster 0 */
    spoil_stream(0, 24, zero, 8);                   /* DataLength 0, valid still 65536 */
    mount("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, find("lfblocks.fb4", &f));
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    TEST_ASSERT_EQUAL_INT(CARD_NONE, map.kind);
    remove("scratch.img");
}

/* A root directory with no end whose FAT entry points back to itself */
void test_root_directory_loop(void) {
    exfat_vol v;
    exfat_file f;
    patched("f.img", 0, "", 0);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(0, exfat_mount(image_read, &v));
    fclose(image);
    image = NULL;
    FILE *fp = fopen("scratch.img", "r+b");
    uint32_t root = exfat_cluster_sector(&v, v.root);
    uint8_t cl[4096];
    fseek(fp, (long)root * 512, SEEK_SET);
    TEST_ASSERT_EQUAL_size_t(4096, fread(cl, 1, 4096, fp));
    for (int i = 64; i < 4096; i += 32) {           /* keep bitmap and up-case */
        memset(cl + i, 0, 32);
        cl[i] = 0x05;                               /* a deleted file entry */
    }
    fseek(fp, (long)root * 512, SEEK_SET);
    fwrite(cl, 1, 4096, fp);
    uint8_t self[4];
    memcpy(self, &v.root, 4);                       /* little-endian host */
    fseek(fp, (long)v.fat * 512 + 4L * v.root, SEEK_SET);
    fwrite(self, 1, 4, fp);
    fclose(fp);
    mount("scratch.img");
    reads = 0;
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, find("lfblocks.fb4", &f));
    TEST_ASSERT_LESS_THAN_INT(5000, reads);         /* 2 MB of directory at most */
    remove("scratch.img");
}

void test_volume_larger_than_partition(void) {
    exfat_vol v;
    uint8_t smaller[4] = { 0x00, 0x20, 0x00, 0x00 };   /* 8192 sectors, not 16384 */
    patched("f.img", 446 + 12, smaller, 4);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(ERR_BLK_PARSE_FAIL, exfat_mount(image_read, &v));
    remove("scratch.img");
}

/* ------------------------------------------------------- blocks.c on a card */

static uint32_t buf[BLK_MAX_LENGTH * BLOCK_SIZE_CELLS];

void test_blocks_on_card_image(void) {
    uint32_t capacity;
    patched("a.img", 0, "", 0);                     /* a copy to write to */
    TEST_ASSERT_EQUAL_INT(0, blk_init("scratch.img", &capacity));
    TEST_ASSERT_EQUAL_UINT32(16, capacity);
    TEST_ASSERT_EQUAL_INT(0, blk_read(7, buf));
    TEST_ASSERT_EQUAL_UINT32(7, buf[1]);
    TEST_ASSERT_EQUAL_INT(0, blk_read_imm(14, buf, 2));
    TEST_ASSERT_EQUAL_INT(0, blk_status());
    TEST_ASSERT_EQUAL_UINT32(14, buf[1]);
    TEST_ASSERT_EQUAL_UINT32(15, buf[BLOCK_SIZE_CELLS + 1]);
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_read_imm(15, buf, 2));
    /* a write lands in the file on the card */
    for (int i = 0; i < BLOCK_SIZE_CELLS; i++) {
        buf[i] = 0xC0DE0000u + i;
    }
    TEST_ASSERT_EQUAL_INT(0, blk_write(3, buf));
    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL_INT(0, blk_read(3, buf));
    TEST_ASSERT_EQUAL_HEX32(0xC0DE0005u, buf[5]);
    TEST_ASSERT_EQUAL_INT(0, blk_read(4, buf));
    TEST_ASSERT_EQUAL_UINT32(4, buf[1]);            /* its neighbor is untouched */
    remove("scratch.img");
}

void test_sectors_and_fence(void) {
    uint32_t capacity;
    patched("a.img", 0, "", 0);
    open_image("scratch.img");
    TEST_ASSERT_EQUAL_INT(0, card_find_blocks(image_read, 0, &map, msg, sizeof msg));
    fclose(image);
    image = NULL;
    TEST_ASSERT_EQUAL_INT(0, blk_init("scratch.img", &capacity));
    /* the MBR can be read, not written */
    TEST_ASSERT_EQUAL_INT(0, blk_sector_read(0, buf, 1));
    TEST_ASSERT_EQUAL_HEX8(0x55, ((uint8_t *)buf)[510]);
    TEST_ASSERT_EQUAL_INT(ERR_BLK_WRITE_PROTECTED, blk_sector_write_imm(0, buf, 1));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_WRITE_PROTECTED, blk_write_fence(2047, 2));
    TEST_ASSERT_EQUAL_INT(0, blk_write_fence(2048, 1));
    TEST_ASSERT_EQUAL_INT(0, blk_write_fence(2048 + 16384 - 64, 64));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_WRITE_PROTECTED, blk_write_fence(2048 + 16384 - 63, 64));
    /* the volume's boot sector, through the sector words */
    TEST_ASSERT_EQUAL_INT(0, blk_sector_read(2048, buf, 1));
    TEST_ASSERT_EQUAL_MEMORY("EXFAT   ", (uint8_t *)buf + 3, 8);
    /* a sector write inside the volume, read back as a block */
    TEST_ASSERT_EQUAL_INT(0, blk_sector_read(map.base, buf, 8));   /* block 0 */
    ((uint8_t *)buf)[100] = 'Z';
    TEST_ASSERT_EQUAL_INT(0, blk_sector_write_imm(map.base, buf, 1));
    TEST_ASSERT_EQUAL_INT(0, blk_status());
    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL_INT(0, blk_read(0, buf));
    TEST_ASSERT_EQUAL_UINT8('Z', ((uint8_t *)buf)[100]);
    /* counts and bounds */
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_sector_read_imm(0, buf, 0));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_sector_read_imm(0, buf, BLK_MAX_SECTORS + 1));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_sector_read_imm(2048 + 16384, buf, 1));
    TEST_ASSERT_EQUAL_INT(0, blk_sector_read_imm(2048 + 16384 - 1, buf, 1));
    remove("scratch.img");
}

void test_plain_block_file_has_no_fence(void) {
    uint32_t capacity;
    FILE *f = fopen("scratch.img", "wb");
    memset(buf, ' ', BLK_SIZE_BYTES);
    memcpy(buf, BLK_SIGNATURE, BLK_SIGNATURE_LEN);
    fwrite(buf, 1, BLK_SIZE_BYTES, f);
    fwrite(buf, 1, BLK_SIZE_BYTES, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, blk_init("scratch.img", &capacity));
    TEST_ASSERT_EQUAL_UINT32(2, capacity);
    TEST_ASSERT_EQUAL_INT(0, blk_sector_read(8, buf, 8));          /* block 1 */
    TEST_ASSERT_EQUAL_INT(ERR_BLK_WRITE_PROTECTED, blk_sector_write_imm(8, buf, 1));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_sector_read_imm(16, buf, 1));
    remove("scratch.img");
}

/* exfat_list: names joined with '|', directories ending in '/' */
static char listed[1024];
static int list_stop_after;             /* stop after this many, 0 = never */
static int list_count;

static int list_name(void *ctx, const char *name, const exfat_file *f) {
    (void)ctx;
    strcat(listed, name);
    if (f->flags & EXFAT_DIRECTORY) {
        strcat(listed, "/");
    }
    strcat(listed, "|");
    if (++list_count == list_stop_after) {
        return 77;
    }
    return 0;
}

static int list(void) {
    listed[0] = '\0';
    list_count = 0;
    return exfat_list(image_read, &vol, vol.root, list_name, NULL);
}

void test_list_root(void) {
    mount("a.img");
    list_stop_after = 0;
    TEST_ASSERT_EQUAL_INT(0, list());
    /* deleted copies and padding skipped, bad.txt's checksum fails */
    TEST_ASSERT_EQUAL_STRING("notes.txt|lfblocks.fb4|A long file name.dat|frag.bin|"
                             "fatc.bin|half.bin|sub/|empty.txt|", listed);
}

void test_list_fragmented_root(void) {
    mount("b.img");                     /* 512-byte clusters, the root fragmented */
    list_stop_after = 0;
    TEST_ASSERT_EQUAL_INT(0, list());
    TEST_ASSERT_EQUAL_STRING("x.bin|lfblocks.fb4|", listed);
}

void test_list_stops_when_asked(void) {
    mount("a.img");
    list_stop_after = 2;
    TEST_ASSERT_EQUAL_INT(77, list());
    TEST_ASSERT_EQUAL_STRING("notes.txt|lfblocks.fb4|", listed);
    list_stop_after = 0;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mount_mbr_card);
    RUN_TEST(test_mount_without_mbr);
    RUN_TEST(test_mount_dirty);
    RUN_TEST(test_mount_rejects_others);
    RUN_TEST(test_find_any_case);
    RUN_TEST(test_find_not_there);
    RUN_TEST(test_find_long_name_after_deleted_copy);
    RUN_TEST(test_find_records_entry_position);
    RUN_TEST(test_find_fragmented_and_fat_chained);
    RUN_TEST(test_find_valid_length_dir_empty);
    RUN_TEST(test_find_ignores_bad_checksum);
    RUN_TEST(test_find_through_fragmented_root);
    RUN_TEST(test_sum16);
    RUN_TEST(test_card_signed_file);
    RUN_TEST(test_card_unsigned_file);
    RUN_TEST(test_card_fragmented_file);
    RUN_TEST(test_card_partial_block_and_bounds);
    RUN_TEST(test_card_nothing);
    RUN_TEST(test_first_cluster_past_the_volume);
    RUN_TEST(test_valid_past_length_without_clusters);
    RUN_TEST(test_root_directory_loop);
    RUN_TEST(test_volume_larger_than_partition);
    RUN_TEST(test_blocks_on_card_image);
    RUN_TEST(test_sectors_and_fence);
    RUN_TEST(test_plain_block_file_has_no_fence);
    RUN_TEST(test_list_root);
    RUN_TEST(test_list_fragmented_root);
    RUN_TEST(test_list_stops_when_asked);
    return UNITY_END();
}
