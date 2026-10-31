/*
 * YMODEM receiver that writes files into blocks (`ymodem`, documented in
 * ymodem.h). The transfer side follows YMODEM batch with CRC-16: the
 * receiver asks with 'C'; each file starts with packet 0 (its name, a NUL,
 * then its length in decimal), answered with ACK and another 'C'; data
 * packets are SOH (128 bytes) or STX (1K), block number, its complement,
 * the data and a CRC-16 (big-endian), answered with ACK or NAK. The end of
 * a file is EOT (NAKed once, then ACKed), and an empty packet 0 ends the
 * batch. CAN CAN cancels. The source side splits the data into lines and
 * blocks with the same `( BLOCK ... )` markers as scripts/putblocks.py; the
 * binary side lays a file out as scripts/putbin.py does; the install side
 * writes a whole block file into the block space (blocks.h).
 */
#include <stdint.h>
#include <string.h>
#include "options.h"
#include "errcodes.h"
#include "serial_io.h"
#include "lftime.h"
#include "lfblocks.h"
#include "tools.h"
#include "vm.h"
#include "api0.h"
#include "blocks.h"
#include "blake2s.h"
#include "sealed.h"
#include "ymodem.h"
#include "memalloc.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define SUB 0x1A
#define CRC 'C'

#ifndef YMODEM_START_SECONDS
#define YMODEM_START_SECONDS 60         // wait this long for the sender
#endif
#define POLL_US     3000000u            // 'C' this often until a packet starts
#define BYTE_US     1000000u            // longest gap inside a packet
#define PACKET_US  10000000u            // longest wait for the next packet
#define QUIET_US     200000u            // purge: line quiet for this long
#define YMODEM_RETRIES   10             // bad packets in a row before giving up
#define BIN_OFFSET  32                  // binary files start at this byte (putbin.py)

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
            uint64_t v = 0;
            while ((i < len) && (s[i] >= '0') && (s[i] <= '9')) {
                v = v * 10 + (uint64_t)(s[i++] - '0');
                if (v > UINT32_MAX) v = UINT32_MAX + 1ull;  // stays too big
                digits++;
            }
            if (digits == 0) return 0;
            // Block numbers are 32-bit. A bigger number becomes UINT32_MAX,
            // which no storage reaches (capacity <= UINT32_MAX), so it is
            // reported as past the end of the block storage.
            *n = (v > UINT32_MAX) ? UINT32_MAX : (uint32_t)v;
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
        problem(st, ERR_YMODEM_SOURCE, "block 0 holds the signature and isn't written", 0);
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
            problem(st, ERR_YMODEM_SOURCE, "( BLOCK++ ) before any numbered block", -1);
            return;
        }
        startBlock(st, (st->last >= UINT32_MAX) ? UINT32_MAX : (uint32_t)st->last + 1);
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
        problem(st, ERR_YMODEM_SOURCE, "line is longer than 128 bytes", st->current);
        return;
    }
    if (st->rows == BLOCK_ROWS) {
        problem(st, ERR_YMODEM_SOURCE, "more than 32 lines", st->current);
        return;
    }
    memcpy(&st->buf[st->rows * ROW_BYTES], s, (size_t)len);
    st->rows++;
}

// Feeds size bytes of the file; padding is a SUB that may be the last
// packet's padding (only when the header gave no length)
static void sourceBytes(src_state* st, const uint8_t* data, int size, int padding) {
    for (int i = 0; i < size; i++) {
        char c = (char)data[i];
        if (padding && (c == SUB)) continue;
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
 * The binary side: a stream from byte 32 of a block, as putbin.py writes
 */
typedef struct {
    uint32_t next;                      // where the next file starts
    uint32_t block;                     // block being filled
    char* buf;                          // its buffer, or NULL
    int pos;                            // bytes in it so far
    uint32_t first;                     // this file's first block
    int ior;                            // this file's first problem, or 0
    const char* err_msg;
} bin_state;

static int binStartBlock(bin_state* b) {
    uint32_t* data = NULL;
    int ior = lfBlockBuffer(b->block, &data);
    if (ior) {
        b->ior = ior;
        b->err_msg = (ior == ERR_BLK_BOUNDS) ? "past the end of the block storage"
                                             : "no block buffer";
        b->buf = NULL;
        return ior;
    }
    b->buf = (char*)data;
    memset(b->buf, 0, BLOCK_BYTES);
    b->pos = 0;
    return 0;
}

static void binFlush(bin_state* b) {
    if (b->buf == NULL) return;
    b->buf = NULL;
    if (b->ior) return;
    lfAPI_update();
    int ior = lfAPI_saveBuffers();
    if (ior) {
        b->ior = ior;
        b->err_msg = "can't write the block";
    }
}

// Starts a file of length bytes (or -1: unknown) at b->next
static void binBegin(bin_state* b, int64_t length) {
    b->ior = 0;
    b->err_msg = NULL;
    b->buf = NULL;
    b->first = b->block = b->next;
    if (length < 0) {
        b->ior = ERR_YMODEM_SOURCE;
        b->err_msg = "the header gives no length, so it can't be placed";
        return;
    }
    uint64_t blocks = ((uint64_t)length + BIN_OFFSET + BLOCK_BYTES - 1) / BLOCK_BYTES;
    if ((uint64_t)b->first + blocks > g_block_capacity) {
        b->ior = ERR_BLK_BOUNDS;
        b->err_msg = "doesn't fit before the end of the block storage";
        return;
    }
    if (binStartBlock(b) == 0) b->pos = BIN_OFFSET;
}

static void binBytes(bin_state* b, const uint8_t* data, int size) {
    while ((size > 0) && (b->buf != NULL)) {
        int n = BLOCK_BYTES - b->pos;
        if (n > size) n = size;
        memcpy(b->buf + b->pos, data, (size_t)n);
        b->pos += n;
        data += n;
        size -= n;
        if (b->pos == BLOCK_BYTES) {
            binFlush(b);
            if (b->ior) return;
            b->block++;
            if (size > 0) binStartBlock(b);
        }
    }
}

// Ends a file: writes its last block; the next file starts after it
static void binEnd(bin_state* b) {
    if (b->ior == 0) {
        if (b->buf != NULL) {
            binFlush(b);
            b->block++;
        }
        if (b->ior == 0) b->next = b->block;
    }
    b->buf = NULL;
}

/* ------------------------------------------------------------------------
 * The install side: a whole block file, block 0 included, written into
 * the space the block file has (blk_space), as it is. Block 0 comes first,
 * so it's checked (blk_check_image) before anything is written. While the
 * other blocks go in, block 0 is MARKER, a plain block file's block 0 that
 * says the install didn't finish; the real one is written last. Then
 * everything is read back and compared with what came, by BLAKE2s, and
 * the file is mounted again.
 */
#define MARKER BLK_SIGNATURE " ( an install by -1 ymodem didn't finish: send the block file again )"

typedef struct {
    int on;                             // this file is being installed
    int stage;                          // -2 ymodem: a package into lfstage.bin
    uint32_t blocks;                    // blocks in the image
    uint32_t block;                     // block being filled
    int pos;                            // bytes in it so far
    int started;                        // MARKER is in block 0: the old file is gone
    int done;                           // all of it written, block 0 too
    int ior;                            // the first problem, or 0
    const char* err_msg;
    char msg[120];                      // blk_check_image's reason
    blake2s_state hash;                 // of the bytes that came
} inst_state;

// From the pool while ymodem runs (lfAPI_ymodem), like report and the
// packet buffer: none of them is needed the rest of the time
static uint32_t* inst_b0;               // block 0, written last
static uint32_t* inst_buf;              // the block being filled

// Writes or reads block blk of where the file goes
static int instWrite(const inst_state* t, uint32_t blk, const uint32_t* buf) {
    return t->stage ? blk_stage_write(blk, buf) : blk_raw_write(blk, buf, 1);
}

static int instRead(const inst_state* t, uint32_t blk, uint32_t* buf) {
    return t->stage ? blk_stage_read(blk, buf) : blk_raw_read(blk, buf, 1);
}

static uint8_t* instBuffer(inst_state* t) {
    return (uint8_t*)((t->block == 0) ? inst_b0 : inst_buf);
}

static void instFail(inst_state* t, int ior, const char* msg) {
    if (t->ior == 0) {
        t->ior = ior;
        t->err_msg = msg;
    }
}

// Starts a file of length bytes (or -1: unknown); stage: -2 ymodem
static void instBegin(inst_state* t, int64_t length, int files, int stage) {
    memset(t, 0, sizeof(*t));
    t->stage = stage;
    if (files > 1) {
        instFail(t, ERR_YMODEM_SOURCE, stage ? "one package at a time" : "one block file at a time");
        return;
    }
    if (length < 0) {
        instFail(t, ERR_YMODEM_SOURCE, "the header gives no length");
        return;
    }
    if ((length == 0) || (length % BLOCK_BYTES)) {
        instFail(t, ERR_YMODEM_SOURCE, stage ? "a package is a whole number of 4 KB blocks"
                                             : "a block file is a whole number of 4 KB blocks");
        return;
    }
    uint32_t room = stage ? blk_stage_space() : blk_space();
    if (length / BLOCK_BYTES > room) {
        instFail(t, ERR_BLK_BOUNDS,
                 stage ? (room ? "it's bigger than lfstage.bin" : "there's no lfstage.bin to stage into")
                       : (room ? "it's bigger than the block file's space"
                               : "there's no block file to install into"));
        return;
    }
    t->on = 1;
    t->blocks = (uint32_t)(length / BLOCK_BYTES);
    lfAPI_emptyBuffers();               // nothing old written over the new
    b2s_init(&t->hash, 32, NULL, 0, NULL, NULL);
}

// A block has come
static void instBlock(inst_state* t) {
    int ior;
    if (t->block == 0 && t->stage) {
        if (memcmp(inst_b0, PACKAGE_MAGIC, SEALED_MAGIC_LEN) != 0) {
            instFail(t, ERR_YMODEM_SOURCE,
                     "not a firmware update package (block 0 doesn't start with " PACKAGE_MAGIC ")");
            return;
        }
        memset(inst_buf, 0, BLOCK_BYTES);       // not a package until block 0 is in
        ior = blk_stage_write(0, inst_buf);
        if (ior == 0) t->started = 1;
    } else if (t->block == 0) {
        const uint8_t* b0 = (const uint8_t*)inst_b0;
        if (blk_check_image(b0, t->blocks, t->msg, sizeof(t->msg))) {
            instFail(t, ERR_YMODEM_SOURCE, t->msg);
            return;
        }
        memset(inst_buf, ' ', BLOCK_BYTES);
        memcpy(inst_buf, MARKER, sizeof(MARKER) - 1);
        ior = blk_raw_write(0, inst_buf, 1);
        if (ior == 0) t->started = 1;
    } else {
        ior = instWrite(t, t->block, inst_buf);
    }
    if (ior) instFail(t, ior, "can't write the block");
}

static void instBytes(inst_state* t, const uint8_t* data, int size) {
    if (!t->on || t->ior) return;
    b2s_puts(&t->hash, data, size);
    while ((size > 0) && (t->ior == 0)) {
        int n = BLOCK_BYTES - t->pos;
        if (n > size) n = size;
        memcpy(instBuffer(t) + t->pos, data, (size_t)n);
        t->pos += n;
        data += n;
        size -= n;
        if (t->pos == BLOCK_BYTES) {
            instBlock(t);
            t->block++;
            t->pos = 0;
        }
    }
}

// Writes block 0 and reads everything back; returns 0 if it's all there
static int instFinish(inst_state* t) {
    uint8_t want[32], got[32];
    blake2s_state h;
    int ior = instWrite(t, 0, inst_b0);
    if (ior) {
        instFail(t, ior, "can't write block 0");
        return ior;
    }
    t->done = 1;
    b2s_final(&t->hash, want);
    b2s_init(&h, 32, NULL, 0, NULL, NULL);
    for (uint32_t i = 0; i < t->blocks; i++) {
        ior = instRead(t, i, inst_buf);
        if (ior) {
            instFail(t, ior, "can't read it back");
            return ior;
        }
        b2s_puts(&h, (const uint8_t*)inst_buf, BLOCK_BYTES);
        lfWatchdogPing();
    }
    b2s_final(&h, got);
    if (memcmp(want, got, sizeof(want))) {
        instFail(t, ERR_BLK_WRITE_FAIL, "what was read back differs from what came");
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}

// Ends a file; complete: all of its bytes came
static void instEnd(inst_state* t, int complete) {
    if (t->on && (t->ior == 0)) {
        if (!complete || (t->block != t->blocks)) {
            instFail(t, ERR_YMODEM_TRANSFER, "the transfer stopped");
        } else {
            instFinish(t);
        }
    }
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
    lf_putc(CAN);
    lf_putc(CAN);
}

static uint16_t crc16(const uint8_t* p, int n) {
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

enum { PKT_BAD = -1 };

// Reads the rest of a packet that started with c (SOH or STX) into data.
// Returns its block number, or PKT_BAD for a bad or incomplete packet.
static int readPacket(int c, uint8_t* data, int* size) {
    *size = (c == SOH) ? 128 : 1024;
    int blk = readByte(lfGetTimeMicroSec() + BYTE_US);
    int nblk = (blk < 0) ? -1 : readByte(lfGetTimeMicroSec() + BYTE_US);
    if ((blk < 0) || (nblk < 0) || ((blk + nblk) != 255)) return PKT_BAD;
    for (int i = 0; i < *size; i++) {
        int b = readByte(lfGetTimeMicroSec() + BYTE_US);
        if (b < 0) return PKT_BAD;
        data[i] = (uint8_t)b;
    }
    int hi = readByte(lfGetTimeMicroSec() + BYTE_US);
    int lo = (hi < 0) ? -1 : readByte(lfGetTimeMicroSec() + BYTE_US);
    if ((lo < 0) || ((uint16_t)((hi << 8) | lo) != crc16(data, *size))) return PKT_BAD;
    return blk;
}

/* The reports wait in a buffer until the batch ends: printed between files,
   a 'C' in them would look like the receiver's request to the sender. */
#define REPORT_BYTES 1536
static char* report;                    // REPORT_BYTES, from the pool
static int report_len;

static void rPuts(const char* s) {
    while (*s && (report_len < REPORT_BYTES - 5)) report[report_len++] = *s++;
    if (*s) {                           // full: say so once
        memcpy(&report[REPORT_BYTES - 5], "...\n", 4);
        report_len = REPORT_BYTES - 1;
    }
}

static void rCR(void) {
    rPuts("\n");
}

static void rFlush(void) {
    for (int i = 0; i < report_len; i++) {
        if (report[i] == '\n') lfCR();
        else lf_putc(report[i]);
    }
    report_len = 0;
}

// Adds v in decimal; block numbers can exceed INT32_MAX
static void putU(uint32_t v) {
    char buf[11];
    char* p = &buf[sizeof(buf) - 1];
    *p = '\0';
    do {
        *--p = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    rPuts(p);
}

static void reportSource(const char* name, const src_state* st) {
    rPuts("ymodem: ");
    rPuts(name);
    rPuts(": ");
    putU(st->written);
    rPuts(st->written == 1 ? " block written" : " blocks written");
    for (uint32_t i = 0; (i < st->written) && (i < SHOW_BLOCKS); i++) {
        rPuts(i ? " " : ": ");
        putU(st->shown[i]);
    }
    if (st->written > SHOW_BLOCKS) rPuts(" ...");
    rCR();
    if (st->skipped) {
        rPuts("note: ");
        putU(st->skipped);
        rPuts(" non-blank line(s) outside blocks ignored");
        rCR();
    }
    if (st->ior) {
        rPuts("line ");
        putU(st->err_line);
        if (st->err_block >= 0) {
            rPuts(", block ");
            putU((uint32_t)st->err_block);
        }
        rPuts(": ");
        rPuts(st->err_msg);
        rPuts("; nothing written from there on");
        rCR();
    }
}

static void reportBinary(const char* name, const bin_state* b, uint32_t length) {
    rPuts("ymodem: ");
    rPuts(name);
    rPuts(": ");
    if (b->ior) {
        rPuts(b->err_msg);
        if (b->ior != ERR_YMODEM_SOURCE) {
            rPuts(" (block ");
            putU(b->block);
            rPuts(")");
        }
        rPuts("; not written");
    } else {
        putU(length);
        rPuts(" bytes from byte 32 of block ");
        putU(b->first);
        if (b->next - 1 != b->first) {
            rPuts(" to block ");
            putU(b->next - 1);
        }
        rPuts(", next free block ");
        putU(b->next);
    }
    rCR();
}

static void reportStage(const char* name, const inst_state* t, int check, const char* msg) {
    rPuts("ymodem: ");
    rPuts(name);
    rPuts(": ");
    if (t->ior == 0) {
        rPuts("staged in lfstage.bin, ");
        putU(t->blocks);
        rPuts(" blocks, read back and checked");
        rCR();
        rPuts(check ? "ymodem: package refused: " : "ymodem: package: ");
        rPuts(msg);
        rPuts(check ? "; stage 1 won't install it" : "; waiting for stage 1 to install it");
        rCR();
        return;
    }
    rPuts(t->err_msg);
    if (t->started && !t->done) {
        rPuts(" (block ");
        putU(t->block);
        rPuts(")");
    }
    rPuts(t->started ? "; lfstage.bin no longer holds a package: send it again"
                     : "; nothing written");
    rCR();
}

static void reportInstall(const char* name, const inst_state* t) {
    rPuts("ymodem: ");
    rPuts(name);
    rPuts(": ");
    if (t->ior == 0) {
        rPuts("installed, ");
        putU(t->blocks);
        rPuts(" blocks, read back and checked; capacity now ");
        putU(g_block_capacity);
        rCR();
        return;
    }
    rPuts(t->err_msg);
    if (t->started && !t->done) {
        rPuts(" (block ");
        putU(t->block);
        rPuts(")");
    }
    if (t->started) {
        rPuts("; block 0 now says the install didn't finish: send the file again");
    } else {
        rPuts("; nothing written");
    }
    rCR();
}

// Waits for a packet to start, sending 'C' every POLL_US until give_up.
// Returns SOH, STX, EOT or CAN, or -1 on timeout.
static int awaitPacket(uint64_t give_up) {
    uint64_t now = lfGetTimeMicroSec();
    while (now < give_up) {
        lf_putc(CRC);
        uint64_t next = now + POLL_US;
        if (next > give_up) next = give_up;
        int c;
        while ((c = readByte(next)) >= 0) {
            if ((c == SOH) || (c == STX) || (c == EOT) || (c == CAN)) return c;
            // anything else (the newline after `ymodem`, say) is dropped
        }
        now = lfGetTimeMicroSec();
    }
    return -1;
}

#define NO_SENDER 2                     // readHeader: nothing came

// Reads the header packet. Returns 1 with name and length for a file
// (length -1 if not given), 0 for the empty header that ends the batch,
// NO_SENDER if no packet came, or a negative error code.
static int readHeader(uint64_t give_up, uint8_t* data, char* name, int64_t* length) {
    int errors = 0;
    while (1) {
        int c = awaitPacket(give_up);
        if (c < 0) return NO_SENDER;
        if (c == CAN) {
            if (readByte(lfGetTimeMicroSec() + BYTE_US) == CAN) return ERR_YMODEM_TRANSFER;
            continue;
        }
        if (c == EOT) {                 // the end of a file we already ACKed
            lf_putc(ACK);
            continue;
        }
        int size;
        int blk = readPacket(c, data, &size);
        if (blk == PKT_BAD) {
            purge();
            if (++errors >= YMODEM_RETRIES) return ERR_YMODEM_TRANSFER;
            lf_putc(NAK);
            continue;
        }
        if (blk != 0) {                 // an XMODEM sender: no header
            cancel();
            rPuts("ymodem: the sender didn't send a YMODEM header (an XMODEM sender?)");
            rCR();
            return ERR_YMODEM_TRANSFER;
        }
        lf_putc(ACK);
        if (data[0] == 0) return 0;     // end of the batch
        int i = 0;
        while ((i < size - 1) && (i < 63) && data[i]) {
            name[i] = (char)data[i];
            i++;
        }
        name[i] = '\0';
        while ((i < size) && data[i]) i++;          // a longer name
        i++;
        *length = -1;
        if ((i < size) && (data[i] >= '0') && (data[i] <= '9')) {
            int64_t v = 0;
            while ((i < size) && (data[i] >= '0') && (data[i] <= '9') && (v < (1ll << 40))) {
                v = v * 10 + (data[i++] - '0');
            }
            *length = v;
        }
        return 1;
    }
}

#define PACKET_BYTES 1024
// The buffers ymodem takes from the pool while it runs, in cells
#define YMODEM_POOL_CELLS (2 * BLOCK_SIZE_CELLS + REPORT_BYTES / 4 + PACKET_BYTES / 4)

static int ymodemRun(uint32_t first, uint8_t* data);

int lfAPI_ymodem(void) {
    uint32_t first = vmPop();
    uint32_t* mem = pool_alloc(YMODEM_POOL_CELLS);
    if (mem == NULL) return ERR_ALLOCATE_FAILED;    // a flash page open, or the pool used up
    inst_b0 = mem;
    inst_buf = mem + BLOCK_SIZE_CELLS;
    report = (char*)(mem + 2 * BLOCK_SIZE_CELLS);
    uint8_t* data = (uint8_t*)(mem + 2 * BLOCK_SIZE_CELLS + REPORT_BYTES / 4);
    int result = ymodemRun(first, data);
    memset(mem, 0, YMODEM_POOL_CELLS * 4);
    inst_b0 = inst_buf = NULL;
    report = NULL;
    int free_res = pool_free(mem);
    return result ? result : free_res;
}

static int ymodemRun(uint32_t first, uint8_t* data) {
    static src_state st;
    static bin_state bin;
    static inst_state inst;
    char name[64], inst_name[64] = "";
    int stage = (first == YMODEM_STAGE);
    int install = (first == YMODEM_INSTALL) || stage;
    int binary = (first != 0) && !install;
    int inst_complete = 0, inst_extra = 0;
    memset(&inst, 0, sizeof(inst));
    memset(&bin, 0, sizeof(bin));
    bin.next = first;

    /* Re-mount the block space before the transfer, so the staging and
       install sizes (blk_stage_space, blk_space) come from the card as it is
       now, not a view left stale by an earlier operation. On the board a
       stale view gave spurious -92 (ERR_BLK_BOUNDS) that a reset cleared;
       this clears it without one. */
    blk_remount(&g_block_capacity);

    lf_puts("ymodem: send the file(s) now (YMODEM)");
    lfCR();
    report_len = 0;
    serial_binary(1);                   // what comes now is data
    int ior = 0, result = 0, files = 0;
    uint64_t give_up = lfGetTimeMicroSec() + (uint64_t)YMODEM_START_SECONDS * 1000000u;
    while (1) {
        int64_t length = -1;
        int h = readHeader(give_up, data, name, &length);
        if ((h == NO_SENDER) && (files == 0)) {
            rPuts("ymodem: no sender");
            rCR();
        }
        if ((h < 0) || (h == NO_SENDER)) {
            ior = ERR_YMODEM_TRANSFER;
            break;
        }
        if (h == 0) break;              // the end of the batch
        files++;
        if (install) {
            if (files == 1) {
                instBegin(&inst, length, files, stage);
                memcpy(inst_name, name, sizeof(inst_name));
            }
        } else if (binary) {
            binBegin(&bin, length);
        } else {
            memset(&st, 0, sizeof(st));
            st.current = -1;
            st.last = -1;
        }
        lf_putc(CRC);                   // ready for the data

        uint8_t expected = 1;
        int errors = 0, eots = 0;
        int64_t left = length;          // bytes still to keep, or -1
        while (1) {
            int c = readByte(lfGetTimeMicroSec() + PACKET_US);
            if (c == EOT) {
                if (eots++ == 0) {
                    lf_putc(NAK);       // YMODEM: NAK the first EOT
                    continue;
                }
                lf_putc(ACK);
                break;
            }
            if (c == CAN) {
                if (readByte(lfGetTimeMicroSec() + BYTE_US) == CAN) {
                    ior = ERR_YMODEM_TRANSFER;  // cancelled by the sender
                    break;
                }
                errors++;
                purge();
                lf_putc(NAK);
            } else if ((c == SOH) || (c == STX)) {
                int size;
                int blk = readPacket(c, data, &size);
                if (blk == PKT_BAD) {
                    errors++;
                    purge();
                    lf_putc(NAK);
                } else if ((uint8_t)blk == expected) {
                    int keep = size;
                    if ((left >= 0) && (left < keep)) keep = (int)left;
                    if (left >= 0) left -= keep;
                    if (install) {
                        if (files == 1) instBytes(&inst, data, keep);
                    } else if (binary) {
                        binBytes(&bin, data, keep);
                    } else {
                        sourceBytes(&st, data, keep, length < 0);
                    }
                    expected++;
                    errors = 0;
                    eots = 0;
                    lf_putc(ACK);
                } else if ((uint8_t)blk == (uint8_t)(expected - 1)) {
                    lf_putc(ACK);               // a repeat of the last one
                    if (blk == 0) lf_putc(CRC); // the header again: our ACK was lost
                } else {
                    cancel();                   // out of step: give up
                    ior = ERR_YMODEM_TRANSFER;
                    break;
                }
            } else {                            // timeout or noise
                errors++;
                purge();
                lf_putc(NAK);
            }
            if (errors >= YMODEM_RETRIES) {
                cancel();
                ior = ERR_YMODEM_TRANSFER;
                break;
            }
        }
        if (ior) break;
        if (install) {                  // finished after the batch
            if (files == 1) {
                inst_complete = 1;
            } else {
                inst_extra++;
            }
        } else if (binary) {            // not for a transfer cut short
            binEnd(&bin);
            reportBinary(name, &bin, (uint32_t)length);
            if ((result == 0) && bin.ior) result = bin.ior;
        } else {
            sourceEnd(&st);
            reportSource(name, &st);
            if ((result == 0) && st.ior) result = st.ior;
        }
        give_up = lfGetTimeMicroSec() + PACKET_US;     // the next header
    }
    serial_binary(0);
    if (stage && files) {
        static sealed_package pkg;
        static char msg[120];
        int check = 0;
        instEnd(&inst, inst_complete);
        if (inst.done && inst.ior == 0) {
            check = blk_stage_check(&pkg, msg, sizeof(msg));
            if (check == 0) {
                blk_stage_ready();
            }
        }
        reportStage(inst_name, &inst, check, msg);
        if (inst.ior && (ior == 0) && (result == 0)) result = inst.ior;
        if (check && (result == 0)) result = ERR_YMODEM_SOURCE;
        if (inst_extra) {
            rPuts("ymodem: ");
            putU((uint32_t)inst_extra);
            rPuts(" more file(s) not written: one package at a time");
            rCR();
            if (result == 0) result = ERR_YMODEM_SOURCE;
        }
    } else if (install && files) {
        instEnd(&inst, inst_complete);
        if (inst.on) {                  // the block file may have changed
            blk_remount(&g_block_capacity);
            lfAPI_emptyBuffers();
        }
        reportInstall(inst_name, &inst);
        if (inst.ior && (ior == 0) && (result == 0)) result = inst.ior;
        if (inst_extra) {
            rPuts("ymodem: ");
            putU((uint32_t)inst_extra);
            rPuts(" more file(s) not written: one block file at a time");
            rCR();
            if (result == 0) result = ERR_YMODEM_SOURCE;
        }
    }
    rFlush();
    if (ior) {
        if (files) {
            lf_puts("ymodem: the transfer failed");
            lfCR();
        }
        return ior;
    }
    return result;
}
