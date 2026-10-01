/* Unit tests for lfCrc32 (src/target/desktop/crc32.c) */
#include "unity.h"
#include "crc32.h"

void setUp(void) {}
void tearDown(void) {}

/* No cells: the CRC of an empty message. */
static void test_crc32_empty(void) {
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, lfCrc32(0, 0));
}

/* "1234" then "5678" as little-endian cells: CRC-32 of "12345678". */
static void test_crc32_check_bytes(void) {
    const int32_t cells[2] = { 0x34333231, 0x38373635 };
    TEST_ASSERT_EQUAL_HEX32(0x9AE0DAAFu, lfCrc32(cells, 2));
}

/* One zero cell and one erased cell, as found in a flash page. */
static void test_crc32_zero_and_erased(void) {
    const int32_t zero = 0;
    const int32_t erased = -1;
    TEST_ASSERT_EQUAL_HEX32(0x2144DF1Cu, lfCrc32(&zero, 1));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, lfCrc32(&erased, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_check_bytes);
    RUN_TEST(test_crc32_zero_and_erased);
    return UNITY_END();
}
