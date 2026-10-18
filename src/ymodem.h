#ifndef YMODEM_H
#define YMODEM_H

/**
 * @brief Forth word `ymodem`  ( n -- )
 * Receives files over the terminal by YMODEM (batch; CRC-16; 128-byte and
 * 1K packets) and writes them into blocks. The header packet of each file
 * gives its name and length, so exactly that many bytes are kept: the
 * padding of the last packet never reaches the blocks.
 *
 * n = 0: each file is Forth source, written the way scripts/putblocks.py
 * does:
 *   - `( BLOCK n )` starts block n, `( BLOCK++ )` the block after the last
 *     one started, and `( BLOCK IGNORE )` skips the lines up to the next
 *     marker, as does text before the first marker. Marker lines aren't
 *     stored.
 *   - Each line becomes a 128-column row, padded with spaces, and the rows
 *     after a block's last line are spaces. A UTF-8 BOM at the start and
 *     CRs are dropped.
 *   Each block is filled in a block buffer (`buffer`, no read) and written
 *   when the next marker or the end of the file comes (`update
 *   save-buffers`). At the first problem (a line longer than 128 bytes,
 *   more than 32 lines in a block, block 0, a block past the end of the
 *   storage, `( BLOCK++ )` before any numbered block, or a write that
 *   fails) it stops writing that file, keeps receiving, and reports the
 *   problem with its line number. The blocks written before it stay
 *   written.
 *
 * n > 0: each file is binary, written the way scripts/putbin.py does: one
 * stream from byte 32 of block n (bytes 0-31 of block n are zeros, kept
 * for an HMAC) on through the blocks after it, the rest of the last block
 * zeros. The next file of the batch starts in the block after the last
 * one. A file that wouldn't fit, or whose header has no length, isn't
 * written.
 *
 * After `ymodem`, start the send in the terminal program (kwai: `~x FILE`
 * types `0 ymodem` or `BLOCK ymodem` itself; in RealTerm: Send tab,
 * YMODEM). `ymodem` asks with `C` every 3 seconds and gives up after
 * YMODEM_START_SECONDS (default 60). It reports each file as it ends: the
 * blocks it wrote and any problem.
 *
 * While it runs, the bytes from the terminal are data, so serial_binary
 * turns off the CH32H417's Ctrl+X x3 escape.
 *
 * @return 0 on success; ERR_YMODEM_TRANSFER if the sender never started,
 *         cancelled, or the transfer failed after YMODEM_RETRIES tries;
 *         ERR_YMODEM_SOURCE for a problem with a file; or the error from
 *         writing a block. With several files, the first problem's.
 */
int lfAPI_ymodem(void);

#endif /* YMODEM_H */
