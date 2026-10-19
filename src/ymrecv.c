/* A small YMODEM receiver for stage 1's recovery mode. See ymrecv.h; the
   protocol is the same as src/ymodem.c's. */
#include <stdio.h>
#include <string.h>
#include "errcodes.h"
#include "ymrecv.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC 'C'

#define POLL_MS     3000                /* 'C' this often until the header starts */
#define BYTE_MS     1000                /* longest gap inside a packet */
#define PACKET_MS  10000                /* longest wait for the next packet */
#define QUIET_MS     200                /* purge: line quiet for this long */
#define RETRIES       10                /* bad packets in a row before giving up */
#define NO_SENDER      2                /* read_header: nothing came */

static const ymr_io *y;
static uint8_t pkt[1024];
static char name[64];

static void purge(void) {
    while (y->get(QUIET_MS) >= 0) {
    }
}

static void cancel(void) {
    y->put(CAN);
    y->put(CAN);
    purge();
}

static uint16_t crc16(const uint8_t *p, int n) {
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

/* The rest of a packet that started with c (SOH or STX), into pkt. Returns
   its block number, or -1 for a bad or incomplete packet. */
static int read_packet(int c, int *size) {
    *size = (c == SOH) ? 128 : 1024;
    int blk = y->get(BYTE_MS);
    int nblk = (blk < 0) ? -1 : y->get(BYTE_MS);
    if (blk < 0 || nblk < 0 || blk + nblk != 255) {
        return -1;
    }
    for (int i = 0; i < *size; i++) {
        int b = y->get(BYTE_MS);
        if (b < 0) {
            return -1;
        }
        pkt[i] = (uint8_t)b;
    }
    int hi = y->get(BYTE_MS);
    int lo = (hi < 0) ? -1 : y->get(BYTE_MS);
    if (lo < 0 || (uint16_t)((hi << 8) | lo) != crc16(pkt, *size)) {
        return -1;
    }
    return blk;
}

/* Sends 'C' every POLL_MS until a packet starts, for up to ms. Returns its
   first byte, or -1. Other bytes (the line end after the command) go. */
static int await_start(int ms) {
    for (int waited = 0; waited < ms; waited += POLL_MS) {
        y->put(CRC);
        int c;
        while ((c = y->get(ms - waited < POLL_MS ? ms - waited : POLL_MS)) >= 0) {
            if (c == SOH || c == STX || c == EOT || c == CAN) {
                return c;
            }
        }
    }
    return -1;
}

/* Packet 0. Returns 1 with name and *length (-1 if none) for a file, 0
   for the empty one that ends the batch, NO_SENDER, or an error code. */
static int read_header(int ms, long *length) {
    for (int errors = 0; ; ) {
        int c = await_start(ms);
        if (c < 0) {
            return NO_SENDER;
        }
        if (c == CAN) {
            if (y->get(BYTE_MS) == CAN) {
                return ERR_YMODEM_TRANSFER;
            }
            continue;
        }
        if (c == EOT) {                 /* the end of a file already ACKed */
            y->put(ACK);
            continue;
        }
        int size, blk = read_packet(c, &size);
        if (blk < 0) {
            purge();
            if (++errors >= RETRIES) {
                return ERR_YMODEM_TRANSFER;
            }
            y->put(NAK);
            continue;
        }
        if (blk != 0) {                 /* an XMODEM sender: no header */
            cancel();
            return ERR_YMODEM_TRANSFER;
        }
        y->put(ACK);
        if (pkt[0] == 0) {
            return 0;
        }
        int i = 0;
        while (i < size && pkt[i] && i < (int)sizeof name - 1) {
            name[i] = (char)pkt[i];
            i++;
        }
        name[i] = 0;
        while (i < size && pkt[i]) {
            i++;
        }
        i++;
        *length = -1;
        if (i < size && pkt[i] >= '0' && pkt[i] <= '9') {
            long v = 0;
            while (i < size && pkt[i] >= '0' && pkt[i] <= '9' && v < (1L << 30)) {
                v = v * 10 + (pkt[i++] - '0');
            }
            *length = v;
        }
        return 1;
    }
}

int ymr_receive(const ymr_io *io, uint32_t *size, char *msg, int len) {
    y = io;
    *size = 0;
    name[0] = 0;
    long length;
    int r = read_header(io->start_ms, &length);
    if (r == NO_SENDER || r == 0) {
        snprintf(msg, len, "ymodem: %s", r ? "no sender started" : "no file came");
        return ERR_YMODEM_TRANSFER;
    }
    if (r < 0) {
        snprintf(msg, len, "ymodem: the transfer was cancelled");
        return r;
    }
    const char *why = NULL;
    if (length <= 0 || length % YMR_BLOCK) {
        why = "isn't whole 4 KB blocks";
    } else if ((unsigned long)length / YMR_BLOCK > io->blocks) {
        why = "is too big";
    }
    if (why) {
        cancel();
        snprintf(msg, len, "ymodem: %s %s: not taken", name, why);
        return ERR_YMODEM_SOURCE;
    }
    y->put(CRC);                        /* now the data */

    uint32_t got = 0;
    uint8_t expected = 1;
    int errors = 0, err = 0;
    while (1) {
        int c = y->get(PACKET_MS);
        if (c == EOT) {
            if (got < (uint32_t)length) {
                why = "ended early";
                break;
            }
            y->put(NAK);                /* once, then the ACK */
            if (y->get(PACKET_MS) == EOT) {
                y->put(ACK);
            }
            break;
        }
        if (c == CAN) {
            if (y->get(BYTE_MS) == CAN) {
                snprintf(msg, len, "ymodem: the sender cancelled");
                return ERR_YMODEM_TRANSFER;
            }
            continue;
        }
        if (c != SOH && c != STX) {
            if (c < 0 || c == NAK) {    /* a lost packet: ask again */
                if (++errors >= RETRIES) {
                    why = "stalled";
                    break;
                }
                y->put(NAK);
            }
            continue;
        }
        int n, blk = read_packet(c, &n);
        if (blk < 0) {
            purge();
            if (++errors >= RETRIES) {
                why = "kept arriving damaged";
                break;
            }
            y->put(NAK);
            continue;
        }
        if (blk == (uint8_t)(expected - 1)) {   /* our ACK was lost: again */
            y->put(ACK);
            continue;
        }
        if (blk != expected) {
            why = "arrived out of order";
            break;
        }
        errors = 0;
        for (int i = 0; i < n && got < (uint32_t)length; i++, got++) {
            uint32_t b = got / YMR_BLOCK, at = got % YMR_BLOCK;
            (b ? io->work : io->block0)[at] = pkt[i];
            if (at == YMR_BLOCK - 1 && b) {
                err = io->write_block(b, io->work);
                if (err) {
                    break;
                }
            }
        }
        if (err) {
            why = "couldn't be written";
            break;
        }
        if (io->magic && expected == 1
            && memcmp(io->block0, io->magic, strlen(io->magic)) != 0) {
            why = "isn't a firmware update package";
            break;
        }
        y->put(ACK);
        expected++;
    }
    if (why) {
        cancel();
        snprintf(msg, len, "ymodem: %s %s: not taken", name, why);
        return err ? err : ERR_YMODEM_SOURCE;
    }
    err = io->write_block(0, io->block0);
    if (err) {
        cancel();
        snprintf(msg, len, "ymodem: %s: block 0 couldn't be written (%d)", name, err);
        return err;
    }
    *size = (uint32_t)length;
    if (read_header(PACKET_MS, &length) == 1) {
        cancel();                       /* one file only: the rest of a batch goes */
    }
    snprintf(msg, len, "ymodem: %s: %lu bytes received", name, (unsigned long)*size);
    return 0;
}
