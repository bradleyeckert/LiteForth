/* Unit tests for the I/O page checks in vmFetch / vmStore (VM_IO_PAGE) */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "vm.h"
#include "errcodes.h"
#include "iopage.h"

/* vm.c's outside references, unused here */
uint32_t g_lf_sys_options;
int VMapi0Call(int fn) { (void)fn; return 0; }
int VMapi1Call(int fn) { (void)fn; return 0; }
uint64_t lfGetTimeMicroSec(void) { return 0; }
void lfWatchdogPing(void) {}

#define PAGE_ADDR(page, cell) (((uint32_t)(page) << (22 - VM_LOG2_PAGES)) | (cell))
#define IO(byte_offset) PAGE_ADDR(VM_IO_PAGE, (byte_offset) / 4)

static void test_ranges(void) {
    assert(vmIoValid(0x00000 / 4));     /* TIM2 */
    assert(vmIoValid(0x10800 / 4));     /* GPIOA */
    assert(vmIoValid(0x21000 / 4));     /* RCC */
    assert(vmIoValid(0x293FC / 4));     /* the last cell of ETH's slots */
    assert(vmIoValid(0x383FC / 4));     /* the last cell, UHSIF */
    assert(!vmIoValid(0x06000 / 4));    /* gap after I2C3 */
    assert(!vmIoValid(0x08800 / 4));    /* gap after SWPMI */
    assert(!vmIoValid(0x29400 / 4));    /* just past ETH */
    assert(!vmIoValid(0x38400 / 4));    /* past the end */
    printf("test_ranges passed!\n");
}

static void test_fetch_store(void) {
    int32_t x = 0;
    assert(vmFetch(IO(0x10800), &x) == 0);
    assert(vmStore(IO(0x10800), 0x12345678) == 0);
    assert(vmFetch(IO(0x10800), &x) == 0 && x == 0x12345678);
    assert(vmFetch(IO(0x06000), &x) == ERR_INVALID_ADDRESS);
    assert(vmStore(IO(0x06000), 1) == ERR_INVALID_ADDRESS);
    assert(vmFetch(IO(0x38400), &x) == ERR_INVALID_ADDRESS);   /* past rd_limit */
    /* other pages aren't checked against the table */
    assert(vmStore(PAGE_ADDR(1, 0x06000 / 4), 7) == 0);
    assert(vmFetch(PAGE_ADDR(1, 0x06000 / 4), &x) == 0 && x == 7);
    printf("test_fetch_store passed!\n");
}

int main(void) {
    printf("Running unit tests for the I/O page...\n");
    /* page 1: plain RAM; the I/O page: an array standing in for registers */
    vm_memory[1] = calloc(0x10000, 4);
    vm_memory_rd_limit[1] = 0x10000;
    vm_memory[VM_IO_PAGE] = calloc(IO_CELLS, 4);
    vm_memory_rd_limit[VM_IO_PAGE] = IO_CELLS;
    vm_memory_wp_limit[VM_IO_PAGE] = 0;
    assert(vm_memory[1] && vm_memory[VM_IO_PAGE]);
    test_ranges();
    test_fetch_store();
    printf("All I/O page tests passed successfully!\n");
    return 0;
}
