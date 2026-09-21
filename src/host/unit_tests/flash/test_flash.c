#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "../../flash.h"
#include "../../errcodes.h"
#include "../../options.h"

#define TEST_FLASH_FILE "test_flash.bin"

void setUp(void) {
    // Clean up test binary prior to each test run
    remove(TEST_FLASH_FILE);
}

void tearDown(void) {
    // Clean up test binary after each test run
    remove(TEST_FLASH_FILE);
}

/* Test 1: flash_init creates a new file filled with 0xFF when file is missing */
void test_flash_init_creates_blank_file(void) {
    int32_t* mem_ptr = NULL;
    int err = flash_init(TEST_FLASH_FILE, &mem_ptr);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(mem_ptr);

    // Verify in-memory initialization to 0xFF
    for (size_t i = 0; i < (FLASH_PAGE_CELLS * RAM_PAGE); i++) {
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, (uint32_t)mem_ptr[i]);
    }

    // Verify file was written to disk with 0xFF bytes
    FILE* f = fopen(TEST_FLASH_FILE, "rb");
    TEST_ASSERT_NOT_NULL(f);
    uint32_t first_cell = 0;
    size_t read_cnt = fread(&first_cell, sizeof(uint32_t), 1, f);
    fclose(f);

    TEST_ASSERT_EQUAL_INT(1, read_cnt);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, first_cell);
}

/* Test 2: flash_program updates both in-memory flashmem and file storage */
void test_flash_program_syncs_memory_and_disk(void) {
    int32_t* mem_ptr = NULL;
    flash_init(TEST_FLASH_FILE, &mem_ptr);

    // Create pattern buffer
    uint32_t page_buf[FLASH_PAGE_CELLS];
    for (int i = 0; i < FLASH_PAGE_CELLS; i++) {
        page_buf[i] = 0xA5A50000 | i;
    }

    int page = 0;
    int err = flash_program(page_buf, page);
    TEST_ASSERT_EQUAL_INT(0, err);

    // 1. Assert in-memory synchronization
    size_t start_idx = page * FLASH_PAGE_CELLS;
    for (int i = 0; i < FLASH_PAGE_CELLS; i++) {
        TEST_ASSERT_EQUAL_HEX32(page_buf[i], (uint32_t)mem_ptr[start_idx + i]);
    }

    // 2. Assert file persistence on disk
    FILE* f = fopen(TEST_FLASH_FILE, "rb");
    TEST_ASSERT_NOT_NULL(f);

    uint32_t disk_buf[FLASH_PAGE_CELLS];
    fseek(f, page * FLASH_PAGE_CELLS * sizeof(uint32_t), SEEK_SET);
    size_t read_cnt = fread(disk_buf, sizeof(uint32_t), FLASH_PAGE_CELLS, f);
    fclose(f);

    TEST_ASSERT_EQUAL_INT(FLASH_PAGE_CELLS, read_cnt);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(page_buf, disk_buf, FLASH_PAGE_CELLS);
}

/* Test 3: flash_program rejects out-of-bound pages */
void test_flash_program_invalid_sector(void) {
    int32_t* mem_ptr = NULL;
    flash_init(TEST_FLASH_FILE, &mem_ptr);

    uint32_t dummy_buf[FLASH_PAGE_CELLS] = {0};

    // Negative page index
    int err1 = flash_program(dummy_buf, -1);
    TEST_ASSERT_EQUAL_INT(ERR_FLASH_INVALID_SECTOR, err1);

    // Page >= RAM_PAGE limit
    int err2 = flash_program(dummy_buf, RAM_PAGE);
    TEST_ASSERT_EQUAL_INT(ERR_FLASH_INVALID_SECTOR, err2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_flash_init_creates_blank_file);
    RUN_TEST(test_flash_program_syncs_memory_and_disk);
    RUN_TEST(test_flash_program_invalid_sector);
    return UNITY_END();
}