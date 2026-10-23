#ifndef DATALOG_H
#define DATALOG_H

/*
 * Encrypted, authenticated data logs on the microSD card
 * (doc/admin/data-logs.md): the file format, its keys, and finding the end
 * of a log after a power cut. Plain C: the caller owns the buffers and the
 * card, so the board writes records by DMA while it fills the next one,
 * and the desktop tests it all on card images (unit_tests/datalog).
 *
 * A log file is a run of DL_RECORD-byte blocks: block 0 the header (the
 * one-off public key E, the encrypted identity and application header, a
 * tag only the board's key can make), then records 1, 2, ..., each 255
 * sample blocks of 128 bytes, encrypted with AES-128-CTR, tagged with
 * Poly1305, and framed at both ends (magic, file_id, sequence number) in
 * the clear so the end of the log can be found without a key.
 */

#include <stdint.h>
#include "ctr.h"

#define DL_RECORD           32768u      /* a block of the file */
#define DL_SAMPLE           128u        /* a sample block */
#define DL_SAMPLES          255u        /* sample blocks per record */
#define DL_PAYLOAD          64u         /* where they start in a record */
#define DL_APP_HEADER       1024u       /* the application header's bytes */
#define DL_SECTOR           512u

#define DL_MAGIC            "LFLOG001"  /* block 0 */
#define DL_ID_DROPOUT       0xFEu       /* a sample block that notes dropouts */
#define DL_FLAG_LAST        1u          /* a record's flags: the log was closed */

/* What the header's encrypted identity carries (32 bytes on the card) */
typedef struct {
    uint8_t uid[8];                     /* the board's UID, as in memory */
    uint64_t start_time;                /* RTC seconds when the log opened */
    uint32_t firmware;                  /* the V5F image's version */
    uint8_t level;                      /* SECURITY_LEVEL */
} dl_identity;

/**
 * Computes k_auth for the one-off key E and server key S: stage 1's
 * service on the board (it holds the board key b), dl_board_auth on the
 * desktop. Returns 0, or nonzero if it couldn't.
 */
typedef int (*dl_auth_fn)(const uint8_t E[32], const uint8_t S[32], uint8_t k_auth[32]);

/* An open log: its keys, in RAM only */
typedef struct {
    ctr_key key;                        /* k_enc */
    uint8_t nonce[8];
    uint8_t file_id[8];
    uint32_t n;                         /* the next record's number, from 1 */
} dl_state;

/**
 * k_auth = BLAKE2s(E || S, key = X25519(b, S), person "LFlogbrd"), from
 * the board's private key b. Refuses an all-zero shared secret (a small
 * order S). Returns 0 or -1.
 */
int dl_board_auth(const uint8_t b[32], const uint8_t E[32], const uint8_t S[32],
                  uint8_t k_auth[32]);

/**
 * Opens a log: derives its keys from e (32 random bytes, wiped here), the
 * server's public key S and auth's k_auth, and writes block 0 into block0
 * (DL_RECORD bytes, cleared first): file_id (8 random bytes), key_id, E,
 * the identity, app_header (DL_APP_HEADER bytes, or NULL for zeros), and
 * the header tag, and block 0's end framed like a record's (n = 0).
 * Returns 0, or -1 if auth failed, S is unusable, or the encryption
 * failed (block0 is then wiped).
 */
int dl_open(dl_state *d, const uint8_t S[32], uint32_t key_id, uint8_t e[32],
            const uint8_t file_id[8], const dl_identity *id, const uint8_t *app_header,
            dl_auth_fn auth, uint8_t *block0);

/**
 * Makes record rec (DL_RECORD bytes; its sample blocks already at
 * DL_PAYLOAD, `samples` of them, 0 to DL_SAMPLES; the rest is cleared):
 * the frame, the encryption, the tag. With last, flags it as the log's
 * last record. Returns the record's number (its block in the file), or 0
 * if samples is out of range, the log is used up, or the encryption
 * failed (the CH32H417's engine): then rec is wiped, never to be written,
 * and the next call makes the same record number.
 */
uint32_t dl_record(dl_state *d, uint8_t *rec, uint32_t samples, int last);

/**
 * Forgets the log's keys.
 */
void dl_close(dl_state *d);

/* Reads one DL_SECTOR-byte sector of the file: sector s from its start */
typedef int (*dl_read_fn)(void *ctx, uint32_t s, uint8_t *buf);

/**
 * After a power cut, without any key: how many blocks of the file at the
 * start of `read` hold the log (block 0 and every record written whole,
 * by their frames at both ends), searching at most `blocks` blocks. Blocks
 * are written in order, so binary search finds the end. buf is a
 * DL_SECTOR-byte work buffer.
 * Returns the count (at least 1), 0 if block 0 isn't a whole log header
 * (not a log, or torn by the power cut), or a negative error from read.
 */
int32_t dl_find_end(dl_read_fn read, void *ctx, uint32_t blocks, uint8_t *buf);

#endif /* DATALOG_H */
