/*
 * XMODEM receiver that writes a source file into blocks (`xmodem`,
 * documented in xmodem.h). The transfer side follows the original XMODEM
 * (checksum) protocol: the receiver starts it with NAK, each packet is
 * SOH (or STX for 1K), block number, its complement, the data and an 8-bit
 * sum, and is answered with ACK or NAK; EOT ends the transfer, CAN CAN
 * cancels it. The source side splits the data into lines and blocks with
 * the same `( BLOCK ... )` markers as scripts/putblocks.py.
 */
#include <stdint.h>
#include <string.h>
#include "options.h"
#include "errcodes.h"
#include "serial_io.h"
#include "lftime.h"
#include "lfblocks.h"
#include "tools.h"
#include "xmodem.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define SUB 0x1A

#ifndef XMODEM_START_SECONDS
#define XMODEM_START_SECONDS 60         // wait this long for the sender
#endif
#define NAK_US      3000000u            // NAK this often until it starts
#define BYTE_US     1000000u            // longest gap inside a packet
#define PACKET_US  10000000u            // longest wait for the next packet
#define QUIET_US     200000u            // purge: line quiet for this long
#define XMODEM_RETRIES   10             // bad packets in a row before giving up

#define ROW_BYTES   SCREEN_COLUMNS                      // 128
#define BLOCK_BYTES (BLOCK_SIZE_CELLS * 4)              // 4096
#define BLOCK_ROWS  (BLOCK_BYTES / ROW_BYTES)           // 32
#define SHOW_BLOCKS 64                  // block numbers listed at the end

/* ------------------------------------------------------------------------
 * The source side: bytes to lines to blocks
 */
typedef struct {
    char line[ROW_BYTES + 4];           // room for a BOM in front of a full row
    int len;                            // bytes in line
    int overflow;                       // the line was longer than line[]
    uint32_t lineno;                    // lines so far
    int64_t current;                    // block being filled, or -1
    int64_t last;                       // last block started, or -1
    char* buf;                          // its buffer
    int rows;                           // its rows so far
    uint32_t skipped;                   // non-blank lines outside blocks
    uint32_t written;                   // blocks written
    uint32_t shown[SHOW_BLOCKS];        // the first of them
    int ior;                            // the first problem, or 0
    uint32_t err_line;                  // its line number
    const char* err_msg;                // and description
    int64_t err_block;                  // the block it's about, or -1
} src_state;

static int isWhite(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')
        || (c == '\f') || (c == '\v');
}

// Recognizes a marker line, as putblocks.py does. Returns 1 for
// `( BLOCK n )` with n in *n, 2 for `( BLOCK++ )`, 3 for `( BLOCK IGNORE )`,
// or 0 for any other line. Anything after the `)` is ignored.
static int marker(const char* s, int len, uint32_t* n) {
    int i = 0;
    if ((len < 1) || (s[i++] != '(')) return 0;
    while ((i < len) && isWhite(s[i])) i++;
    if ((len - i < 5) || memcmp(&s[i], "BLOCK", 5)) return 0;
    i += 5;
    int kind;
    if ((len - i >= 2) && (s[i] == '+') && (s[i + 1] == '+')) {
        i += 2;
        kind = 2;
    } else {
        int ws = i;
        while ((i < len) && isWhite(s[i])) i++;
        if (i == ws) return 0;          // BLOCK needs a space before n
        if ((len - i >= 6) && !memcmp(&s[i], "IGNORE", 6)) {
            i += 6;
            kind = 3;
        } else {
            int digits = 0;
            uint32_t v = 0;
            while ((i < len) && (s[i] >= '0') && (s[i] <= '9')) {
                if (v > 99999999u) return 0;    // far past any block storage
                v = v * 10 + (uint32_t)(s[i++] - '0');
                digits++;
            }
            if (digits == 0) return 0;
            *n = v;
            kind = 1;
        }
    }
    while ((i < len) && isWhite(s[i])) i++;
    return ((i < len) && (s[i] == ')')) ? kind : 0;
}

static void problem(src_state* st, int ior, const char* msg, int64_t blk) {
    if (st->ior == 0) {
        st->ior = ior;
        st->err_line = st->lineno;
        st->err_msg = msg;
        st->err_block = blk;
    }
}

// Writes the block being filled, if any
static void finishBlock(src_state* st) {
    if (st->current < 0) return;
    if (st->ior == 0) {
        lfAPI_update();                 // the buffer lfBlockBuffer assigned
        int ior = lfAPI_saveBuffers();
        if (ior) {
            problem(st, ior, "can't write the block", st->current);
        } else {
            if (st->written < SHOW_BLOCKS) st->shown[st->written] = (uint32_t)st->current;
            st->written++;
        }
    }
    st->current = -1;
}

// Starts block n: a buffer of spaces, no read
static void startBlock(src_state* st, uint32_t n) {
    finishBlock(st);
    st->last = n;
    if (st->ior) return;
    st->current = n;
    if (n == 0) {
        problem(st, ERR_XMODEM_SOURCE, "block 0 holds the signature and isn't written", 0);
        st->current = -1;
        return;
    }
    uint32_t* data = NULL;
    int ior = lfBlockBuffer(n, &data);
    if (ior) {
        problem(st, ior, (ior == ERR_BLK_BOUNDS) ? "past the end of the block storage"
                                                 : "no block buffer", n);
        st->current = -1;
        return;
    }
    st->buf = (char*)data;
    memset(st->buf, ' ', BLOCK_BYTES);
    st->rows = 0;
}

static void endLine(src_state* st) {
    char* s = st->line;
    int len = st->len;
    st->lineno++;
    st->len = 0;
    if ((len > 0) && (s[len - 1] == '\r')) len--;
    if ((st->lineno == 1) && (len >= 3) && !memcmp(s, "\xEF\xBB\xBF", 3)) {
        s += 3;                         // UTF-8 BOM
        len -= 3;
    }
    int overflow = st->overflow || (len > ROW_BYTES);
    st->overflow = 0;
    if (st->ior) return;                // after a problem, only count lines

    uint32_t n = 0;
    switch (overflow ? 0 : marker(s, len, &n)) {
    case 1:
        startBlock(st, n);
        return;
    case 2:
        if (st->last < 0) {
            problem(st, ERR_XMODEM_SOURCE, "( BLOCK++ ) before any numbered block", -1);
            return;
        }
        startBlock(st, (uint32_t)st->last + 1);
        return;
    case 3:
        finishBlock(st);
        return;
    default:
        break;
    }
    if (st->current < 0) {              // outside blocks: ignored
        for (int i = 0; i < len; i++) {
            if (!isWhite(s[i])) {
                st->skipped++;
                break;
            }
        }
        return;
    }
    if (overflow) {
        problem(st, ERR_XMODEM_SOURCE, "line is longer than 128 bytes", st->current);
        return;
    }
    if (st->rows == BLOCK_ROWS) {
        problem(st, ERR_XMODEM_SOURCE, "more than 32 lines", st->current);
        return;
    }
    memcpy(&st->buf[st->rows * ROW_BYTES], s, (size_t)len);
    st->rows++;
}

static void sourceBytes(src_state* st, const uint8_t* data, int size) {
    for (int i = 0; i < size; i++) {
        char c = (char)data[i];
        if (c == SUB) continue;         // padding of the last packet
        if (c == '\n') {
            endLine(st);
        } else if (st->len < (int)sizeof(st->line)) {
            st->line[st->len++] = c;
        } else {
            st->overflow = 1;
        }
    }
}

static void sourceEnd(src_state* st) {
    if ((st->len > 0) || st->overflow) endLine(st);     // no newline at the end
    finishBlock(st);
}

/* ------------------------------------------------------------------------
 * The transfer side
 */

// Returns the next byte, or -1 if none comes before the deadline
static int readByte(uint64_t deadline) {
    while (1) {
        if (serial_ready() > 0) {
            int c = serial_getc();
            if (c >= 0) return c & 0xFF;
        }
        if (lfGetTimeMicroSec() >= deadline) return -1;
        lfWatchdogPing();               // waiting is fine
#ifdef yield2c
        yield2c();                      // as loadTIB does while waiting
#endif
    }
}

// Reads and drops bytes until the line has been quiet for QUIET_US
static void purge(void) {
    while (readByte(lfGetTimeMicroSec() + QUIET_US) >= 0) {
    }
}

static void cancel(void) {
    serial_putc(CAN);
    serial_putc(CAN);
}

// Waits for the sender's first packet, sending NAK every NAK_US.
// Returns its first byte (SOH, STX, EOT or CAN), or -1 on timeout.
static int awaitSender(void) {
    uint64_t now = lfGetTimeMicroSec();
    uint64_t give_up = now + (uint64_t)XMODEM_START_SECONDS * 1000000u;
    while (now < give_up) {
        serial_putc(NAK);
        uint64_t next = now + NAK_US;
        int c;
        while ((c = readByte(next)) >= 0) {
            if ((c == SOH) || (c == STX) || (c == EOT) || (c == CAN)) return c;
            // anything else (the newline after `xmodem`, say) is dropped
        }
        now = lfGetTimeMicroSec();
    }
    return -1;
}

static void report(const src_state* st) {
    lf_puts("xmodem: ");
    lfDot10((int32_t)st->written);
    lf_puts(st->written == 1 ? " block written" : " blocks written");
    for (uint32_t i = 0; (i < st->written) && (i < SHOW_BLOCKS); i++) {
        lf_puts(i ? " " : ": ");
        lfDot10((int32_t)st->shown[i]);
    }
    if (st->written > SHOW_BLOCKS) lf_puts(" ...");
    lfCR();
    if (st->skipped) {
        lf_puts("note: ");
        lfDot10((int32_t)st->skipped);
        lf_puts(" non-blank line(s) outside blocks ignored");
        lfCR();
    }
    if (st->ior) {
        lf_puts("line ");
        lfDot10((int32_t)st->err_line);
        if (st->err_block >= 0) {
            lf_puts(", block ");
            lfDot10((int32_t)st->err_block);
        }
        lf_puts(": ");
        lf_puts(st->err_msg);
        lf_puts("; nothing written from there on");
        lfCR();
    }
}

int lfAPI_xmodem(void) {
    static uint8_t data[1024];
    static src_state st;
    memset(&st, 0, sizeof(st));
    st.current = -1;
    st.last = -1;

    lf_puts("xmodem: send the file now (XMODEM, checksum)");
    lfCR();
    int c = awaitSender();
    if (c < 0) {
        lf_puts("xmodem: no sender");
        lfCR();
        return ERR_XMODEM_TRANSFER;
    }

    uint8_t expected = 1;
    int errors = 0;
    int ior = 0;
    while (1) {
        if (c == EOT) {
            serial_putc(ACK);
            break;
        }
        if (c == CAN) {
            if (readByte(lfGetTimeMicroSec() + BYTE_US) == CAN) {
                ior = ERR_XMODEM_TRANSFER;      // cancelled by the sender
                break;
            }
            errors++;
            purge();
            serial_putc(NAK);
        } else if ((c == SOH) || (c == STX)) {
            int size = (c == SOH) ? 128 : 1024;
            uint64_t deadline = lfGetTimeMicroSec() + BYTE_US;
            int blk = readByte(deadline);
            int nblk = (blk < 0) ? -1 : readByte(lfGetTimeMicroSec() + BYTE_US);
            int ok = (blk >= 0) && (nblk >= 0) && ((blk + nblk) == 255);
            uint8_t sum = 0;
            for (int i = 0; ok && (i < size); i++) {
                int b = readByte(lfGetTimeMicroSec() + BYTE_US);
                if (b < 0) {
                    ok = 0;
                } else {
                    data[i] = (uint8_t)b;
                    sum += (uint8_t)b;
                }
            }
            if (ok) {
                int b = readByte(lfGetTimeMicroSec() + BYTE_US);
                ok = (b >= 0) && ((uint8_t)b == sum);
            }
            if (!ok) {
                errors++;
                purge();
                serial_putc(NAK);
            } else if ((uint8_t)blk == expected) {
                sourceBytes(&st, data, size);
                expected++;
                errors = 0;
                serial_putc(ACK);
            } else if ((uint8_t)blk == (uint8_t)(expected - 1)) {
                serial_putc(ACK);               // a repeat of the last one
            } else {
                cancel();                       // out of step: give up
                ior = ERR_XMODEM_TRANSFER;
                break;
            }
        } else {                                // timeout or noise
            errors++;
            purge();
            serial_putc(NAK);
        }
        if (errors >= XMODEM_RETRIES) {
            cancel();
            ior = ERR_XMODEM_TRANSFER;
            break;
        }
        c = readByte(lfGetTimeMicroSec() + PACKET_US);
    }

    if (ior == 0) {
        sourceEnd(&st);                 // not for a transfer cut short
    }
    report(&st);
    if (ior) {
        lf_puts("xmodem: the transfer failed");
        lfCR();
        return ior;
    }
    return st.ior;
}
