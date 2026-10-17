/*
 * Tests runs of blocks in blk_read_imm and blk_write_imm (desktop
 * blocks.c): 1 to BLK_MAX_LENGTH blocks per call, bounds and length
 * checks, and that a run lands in consecutive blocks.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "blocks.h"
#include "errcodes.h"

#define TEST_FILE   "test_blkrun.fb4"
#define FILE_BLOCKS 12

static uint32_t buf[BLK_MAX_LENGTH * BLOCK_SIZE_CELLS];
static uint32_t capacity;

/* A signed block file of FILE_BLOCKS blocks, block n filled with n */
void setUp(void) {
    FILE *f = fopen(TEST_FILE, "wb");
    TEST_ASSERT_NOT_NULL(f);
    for (uint32_t n = 0; n < FILE_BLOCKS; n++) {
        for (int i = 0; i < BLOCK_SIZE_CELLS; i++) {
            buf[i] = n;
        }
        if (n == 0) {
            memcpy(buf, BLK_SIGNATURE, BLK_SIGNATURE_LEN);
        }
        fwrite(buf, 1, BLK_SIZE_BYTES, f);
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, blk_init(TEST_FILE, &capacity));
    TEST_ASSERT_EQUAL_UINT32(FILE_BLOCKS, capacity);
}

void tearDown(void) {
    remove(TEST_FILE);
}

/* Each block of the run read into buf holds its block number */
static void check_run(uint32_t blk, int length) {
    for (int b = 0; b < length; b++) {
        TEST_ASSERT_EQUAL_UINT32(blk + b, buf[b * BLOCK_SIZE_CELLS]);
        TEST_ASSERT_EQUAL_UINT32(blk + b, buf[b * BLOCK_SIZE_CELLS + BLOCK_SIZE_CELLS - 1]);
    }
}

void test_read_one_and_many(void) {
    for (int length = 1; length <= BLK_MAX_LENGTH; length++) {
        memset(buf, 0xFF, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(0, blk_read_imm(2, buf, length));
        TEST_ASSERT_EQUAL_INT(0, blk_status());
        check_run(2, length);
    }
}

void test_read_to_the_end(void) {
    uint32_t blk = FILE_BLOCKS - BLK_MAX_LENGTH;
    TEST_ASSERT_EQUAL_INT(0, blk_read_imm(blk, buf, BLK_MAX_LENGTH));
    TEST_ASSERT_EQUAL_INT(0, blk_status());
    check_run(blk, BLK_MAX_LENGTH);
}

void test_length_out_of_range(void) {
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_read_imm(1, buf, 0));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_read_imm(1, buf, BLK_MAX_LENGTH + 1));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_read_imm(1, buf, -1));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_write_imm(1, buf, 0));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_NUMERIC, blk_write_imm(1, buf, BLK_MAX_LENGTH + 1));
}

void test_run_past_the_end(void) {
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_read_imm(FILE_BLOCKS - 2, buf, 3));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_read_imm(FILE_BLOCKS, buf, 1));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_read_imm(0xFFFFFFFFu, buf, 2));
    TEST_ASSERT_EQUAL_INT(ERR_BLK_BOUNDS, blk_write_imm(FILE_BLOCKS - 1, buf, 2));
    TEST_ASSERT_EQUAL_INT(0, blk_read_imm(FILE_BLOCKS - 2, buf, 2));
    check_run(FILE_BLOCKS - 2, 2);
}

void test_write_a_run(void) {
    for (int b = 0; b < 3; b++) {
        for (int i = 0; i < BLOCK_SIZE_CELLS; i++) {
            buf[b * BLOCK_SIZE_CELLS + i] = 100 + b;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, blk_write_imm(4, buf, 3));
    TEST_ASSERT_EQUAL_INT(0, blk_status());
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, blk_read_imm(3, buf, 5));
    TEST_ASSERT_EQUAL_UINT32(3, buf[0]);                       /* untouched */
    for (int b = 1; b <= 3; b++) {
        TEST_ASSERT_EQUAL_UINT32(100 + b - 1, buf[b * BLOCK_SIZE_CELLS]);
        TEST_ASSERT_EQUAL_UINT32(100 + b - 1, buf[b * BLOCK_SIZE_CELLS + BLOCK_SIZE_CELLS - 1]);
    }
    TEST_ASSERT_EQUAL_UINT32(7, buf[4 * BLOCK_SIZE_CELLS]);    /* untouched */
}

void test_single_block_calls_unchanged(void) {
    TEST_ASSERT_EQUAL_INT(0, blk_read(5, buf));
    check_run(5, 1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_one_and_many);
    RUN_TEST(test_read_to_the_end);
    RUN_TEST(test_length_out_of_range);
    RUN_TEST(test_run_past_the_end);
    RUN_TEST(test_write_a_run);
    RUN_TEST(test_single_block_calls_unchanged);
    return UNITY_END();
}
