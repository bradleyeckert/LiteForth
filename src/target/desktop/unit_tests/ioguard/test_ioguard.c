/* Tests the guarded page in src/vm.c (vm_io_page, vm_io_ranges): what
   vmFetch and vmStore allow on it, by range and flags, including slices,
   and that other pages are unchanged. unit_tests/vmasm checks that
   vm_rv32.S does the same. */
#include <stdio.h>
#include <stdint.h>
#include "vm.h"
#include "errcodes.h"

/* vm.c's calls out, unused here */
uint32_t g_lf_sys_options;
int VMapi0Call(int fn) { (void)fn; return 0; }
int VMapi1Call(int fn) { (void)fn; return 0; }
uint64_t lfGetTimeMicroSec(void) { return 0; }
void lfWatchdogPing(void) { }
int iomap_rd(int addr, uint32_t *data) { (void)addr; *data = 0; return -9; }   /* no I/O ports here */
int iomap_wr(int addr, uint32_t data) { (void)addr; (void)data; return -9; }

#define PAGE_SHIFT (22 - VM_LOG2_PAGES)
#define IO  3                           // the guarded page
#define RAM 1

static uint32_t io[32], ram[16];
static int failures;

static const vm_io_range ranges[] = {
    {  0,  4, VM_IO_READ | VM_IO_WRITE },
    {  8, 12, VM_IO_READ },
    { 10, 16, VM_IO_READ | VM_IO_WRITE },   // 10 and 11: the first range wins
    { 16, 20, VM_IO_WRITE },
    { 20, 24, 0 },
    {  0,  0, 0 },
    { 24, 32, VM_IO_READ | VM_IO_WRITE },   // after the end: never used
};

static void check(int ok, const char *what, uint32_t cell) {
    if (!ok) {
        printf("FAIL: %s, cell %u\n", what, (unsigned)cell);
        failures++;
    }
}

static uint32_t at(uint32_t page, uint32_t cell) { return (page << PAGE_SHIFT) | cell; }

/* What a read and a write of `cell` should give: 0, or an ior */
static void expect(uint32_t cell, int rd, int wr) {
    uint32_t v = 0x5555;
    io[cell] = 0xA5A5A5A5u;
    int ior = vmFetch(at(IO, cell), &v);
    check(ior == rd, "read's ior", cell);
    check(v == (rd ? 0 : 0xA5A5A5A5u), "read's value", cell);
    ior = vmStore(at(IO, cell), 0x12345678u);
    check(ior == wr, "write's ior", cell);
    check(io[cell] == (wr ? 0xA5A5A5A5u : 0x12345678u), "write's effect", cell);
}

int main(void) {
    vm_memory[RAM] = ram;
    vm_memory_rd_limit[RAM] = 16;
    vm_memory_wp_limit[RAM] = 4;
    vm_memory[IO] = io;                 // read limit 0: all through the list

    /* no guarded page yet: page IO is unreadable */
    for (uint32_t c = 0; c < 32; c++) expect(c, ERR_INVALID_ADDRESS, ERR_INVALID_ADDRESS);

    vm_io_page = IO;
    vm_io_ranges = ranges;
    for (uint32_t c = 0; c < 32; c++) {
        int rd, wr;
        if (c < 4)        { rd = 0;                   wr = 0; }
        else if (c < 8)   { rd = ERR_INVALID_ADDRESS; wr = ERR_INVALID_ADDRESS; }
        else if (c < 12)  { rd = 0;                   wr = ERR_WRITE_PROTECTED; }
        else if (c < 16)  { rd = 0;                   wr = 0; }
        else if (c < 20)  { rd = ERR_INVALID_ADDRESS; wr = 0; }
        else if (c < 24)  { rd = ERR_INVALID_ADDRESS; wr = ERR_WRITE_PROTECTED; }
        else              { rd = ERR_INVALID_ADDRESS; wr = ERR_INVALID_ADDRESS; }
        expect(c, rd, wr);
    }

    /* slices: an 8-bit field at bit 8 of cell 1 */
    uint32_t slice = (8u << 27) | (8u << 22) | at(IO, 1), v;
    io[1] = 0x11223344u;
    check(vmFetch(slice, &v) == 0 && v == 0x33, "slice read", 1);
    check(vmStore(slice, 0xEE) == 0 && io[1] == 0x1122EE44u, "slice write", 1);
    slice = (8u << 27) | (8u << 22) | at(IO, 9);
    io[9] = 0x11223344u;
    check(vmStore(slice, 0xEE) == ERR_WRITE_PROTECTED && io[9] == 0x11223344u,
          "slice write to a read-only range", 9);

    /* other pages keep their limits, with the guard on */
    ram[2] = 7;
    check(vmFetch(at(RAM, 2), &v) == 0 && v == 7, "RAM read", 2);
    check(vmStore(at(RAM, 2), 1) == ERR_WRITE_PROTECTED, "RAM write below wp", 2);
    check(vmStore(at(RAM, 5), 9) == 0 && ram[5] == 9, "RAM write", 5);
    check(vmFetch(at(RAM, 16), &v) == ERR_INVALID_ADDRESS && v == 0, "RAM past its limit", 16);
    check(vmStore(at(RAM, 16), 1) == ERR_INVALID_ADDRESS, "RAM write past its limit", 16);

    /* a page with a read limit: cells under it don't look at the list */
    vm_memory_rd_limit[IO] = 6;
    io[5] = 0x99;
    check(vmFetch(at(IO, 5), &v) == 0 && v == 0x99, "under the read limit", 5);
    check(vmStore(at(IO, 5), 3) == 0 && io[5] == 3, "write under the read limit", 5);
    check(vmFetch(at(IO, 6), &v) == ERR_INVALID_ADDRESS, "past it, no range", 6);
    check(vmFetch(at(IO, 9), &v) == 0, "past it, in a range", 9);

    if (failures) {
        printf("ioguard: %d failures\n", failures);
        return 1;
    }
    printf("ioguard: the guarded page's allowlist is checked\n");
    return 0;
}
