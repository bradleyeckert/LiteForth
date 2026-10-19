/* Runs src/ymrecv.c over stdin/stdout for test_ymrecv.py: the blocks go
 * into the file named by argv[1], room argv[2] blocks, the sender's start
 * wait argv[3] ms. Prints the blocks in the order they were written, the
 * result and the message on stderr. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ymrecv.h"

static FILE *out;

static int get(int ms) {
    struct pollfd p = { 0, POLLIN, 0 };
    unsigned char c;
    if (poll(&p, 1, ms) <= 0 || read(0, &c, 1) != 1) {
        return -1;
    }
    return c;
}

static void put(int c) {
    unsigned char b = (unsigned char)c;
    if (write(1, &b, 1) != 1) {
        exit(3);
    }
}

static int write_block(uint32_t blk, const uint8_t *buf) {
    fprintf(stderr, "write %u\n", (unsigned)blk);
    fseek(out, (long)blk * YMR_BLOCK, SEEK_SET);
    return fwrite(buf, 1, YMR_BLOCK, out) == YMR_BLOCK ? 0 : -1;
}

int main(int argc, char **argv) {
    static uint8_t b0[YMR_BLOCK], work[YMR_BLOCK];
    if (argc < 4 || !(out = fopen(argv[1], "wb"))) {
        return 2;
    }
    ymr_io io = { get, put, write_block, (uint32_t)atoi(argv[2]), "LITEFORTHF", b0, work,
                  atoi(argv[3]) };
    char msg[120];
    uint32_t size;
    int r = ymr_receive(&io, &size, msg, sizeof msg);
    fclose(out);
    fprintf(stderr, "result %d, size %u: %s\n", r, (unsigned)size, msg);
    return 0;
}
