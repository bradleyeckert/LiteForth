#ifndef YMRECV_H
#define YMRECV_H

/*
 * A small YMODEM receiver for stage 1's recovery mode (doc/firmware-updates.md
 * section 7): takes one file into whole 4 KB blocks through callbacks, with
 * none of LiteForth around it. The protocol is the one src/ymodem.c and
 * kwai's ~x speak: batch, CRC-16, 128-byte or 1K packets, the file's length
 * in packet 0, EOT NAKed once and then ACKed, an empty packet 0 at the end,
 * CAN CAN to cancel. unit_tests/ymrecv drives it with a Python sender.
 */

#include <stdint.h>

#define YMR_BLOCK   4096u

typedef struct {
    int (*get)(int ms);                 /* the next byte, or -1 if none comes in ms */
    void (*put)(int c);                 /* sends a byte */
    int (*write_block)(uint32_t blk, const uint8_t *buf);  /* 0 or an error code */
    uint32_t blocks;                    /* room, in blocks */
    const char *magic;                  /* the file must start with this, or NULL */
    uint8_t *block0;                    /* a 4 KB buffer: block 0, written last */
    uint8_t *work;                      /* a 4 KB buffer for the other blocks */
    int start_ms;                       /* how long to wait for the sender */
} ymr_io;

/**
 * Receives one file and writes it into blocks 0 to n-1 with write_block,
 * block 0 last, so a transfer cut short never leaves a whole new file
 * behind. The file must be whole blocks (a multiple of 4 KB, from the
 * length in packet 0), fit in `blocks`, and start with `magic`; otherwise
 * the transfer is cancelled before block 0 is written. Returns 0 with
 * *size its length, or a negative error code (ERR_YMODEM_TRANSFER or
 * ERR_YMODEM_SOURCE, or write_block's), with msg saying why. Nothing is
 * printed through `put` but the protocol, so the caller reports after.
 */
int ymr_receive(const ymr_io *io, uint32_t *size, char *msg, int len);

#endif /* YMRECV_H */
